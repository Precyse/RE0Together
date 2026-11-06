// Ported from MandarinJuice (https://github.com/mi5hmash/MandarinJuice), MIT License, Copyright (c) 2026 Michał Gębicki.
// The full licence text is in THIRD_PARTY.md.
using System.Runtime.InteropServices;
using System.Security.Cryptography;

namespace CoopLauncher.SaveResign;

/// <summary>The "Mandarin" cipher of a save's data: the data is cut into slices of 16 KiB multiples (their sizes come from the
/// game's seed), each slice has a header (its key material wrapped for the owner's user id, the checksum of its data, a stream-cipher
/// layer over the header) and its data XORed with an AES-128 stream. Decrypting needs the user id the data was encrypted for; a
/// wrong one fails the checksum of the first slice. Pure.</summary>
internal sealed class MandarinCipher(ulong seed)
{
    private const int SliceShift = 14;
    private const int SliceCapacityMask = 7;
    private const int KeyMaterialBytes = MandarinKeys.KeyMaterialUlongs * sizeof(ulong);
    private const int WrappedBytes = MandarinKeys.WrappedUlongs * sizeof(ulong);
    private const int SliceHeaderBytes = WrappedBytes + 2 * sizeof(ulong);
    private const int ChecksumSlot = MandarinKeys.WrappedUlongs;
    private const int RemainingSlot = MandarinKeys.WrappedUlongs + 1;
    private const int AesKeyBytes = 16;
    private const int AesBlockBytes = 16;
    private const int KeyRows = 2;
    private const int SecondRowShift = 8;
    private const ulong SplitMixIncrement = 0x9E3779B97F4A7C15;
    private const ulong SplitMixMultiplier1 = 0xBF58476D1CE4E5B9;
    private const ulong SplitMixMultiplier2 = 0x94D049BB133111EB;
    private const int SplitMixShift1 = 30;
    private const int SplitMixShift2 = 27;
    private const int SplitMixShift3 = 31;

    /// <summary>The <paramref name="decryptedLength"/> bytes of data that <paramref name="encrypted"/> (the file's data section) holds for the
    /// user <paramref name="userId"/>. Throws <see cref="InvalidDataException"/> when the section is too short or the user id is not the owner's.</summary>
    public byte[] Decrypt(ReadOnlySpan<byte> encrypted, int decryptedLength, ulong userId)
    {
        var state = seed;
        var sliceLengths = SliceDataLengths(ref state, (uint)decryptedLength);
        if (encrypted.Length < EncryptedLength(sliceLengths)) throw new InvalidDataException("Mandarin data is shorter than its slices");
        state += userId;

        var work = encrypted.ToArray();
        var decrypted = new byte[decryptedLength];
        var keyMaterial = new ulong[MandarinKeys.KeyMaterialUlongs];
        var position = 0;
        var written = 0;
        foreach (var dataLength in sliceLengths)
        {
            var slice = work.AsSpan(position, SliceHeaderBytes + dataLength);
            NextKeyMaterial(ref state);
            var header = slice[..SliceHeaderBytes];
            XorSplitMix(ref state, header);
            var headerWords = MemoryMarshal.Cast<byte, ulong>(header);
            MandarinKeys.Unwrap(headerWords[..MandarinKeys.WrappedUlongs], userId, keyMaterial);
            var data = slice[SliceHeaderBytes..];
            XorAesStream(data, MemoryMarshal.AsBytes<ulong>(keyMaterial));

            var used = Math.Min(dataLength, decryptedLength - written);
            if (position == 0 && MandarinSliceChecksum.Of(data[..used]) != headerWords[ChecksumSlot])
                throw new InvalidDataException("Mandarin data checksum failed: the save was not written for this user id");
            data[..used].CopyTo(decrypted.AsSpan(written));
            position += slice.Length;
            written += used;
        }
        return decrypted;
    }

    /// <summary>The data section of a file that holds <paramref name="plain"/> for the user <paramref name="userId"/>.</summary>
    public byte[] Encrypt(ReadOnlySpan<byte> plain, ulong userId)
    {
        var state = seed;
        var sliceLengths = SliceDataLengths(ref state, (uint)plain.Length);
        var encrypted = new byte[EncryptedLength(sliceLengths)];
        state += userId;
        var personalSeed = MandarinKeys.PersonalSeed(userId);

        var position = 0;
        var consumed = 0;
        foreach (var dataLength in sliceLengths)
        {
            var slice = encrypted.AsSpan(position, SliceHeaderBytes + dataLength);
            var keyMaterial = NextKeyMaterial(ref state);
            var header = slice[..SliceHeaderBytes];
            var headerWords = MemoryMarshal.Cast<byte, ulong>(header);
            MandarinKeys.Wrap(headerWords[..MandarinKeys.WrappedUlongs], MemoryMarshal.Cast<byte, ulong>(keyMaterial), personalSeed);

            var data = slice[SliceHeaderBytes..];
            var used = Math.Min(dataLength, plain.Length - consumed);
            plain.Slice(consumed, used).CopyTo(data);
            headerWords[ChecksumSlot] = MandarinSliceChecksum.Of(data[..used]);
            var remaining = (ulong)(plain.Length - consumed);
            SplitMix(ref remaining);
            headerWords[RemainingSlot] = remaining;

            XorSplitMix(ref state, header);
            XorAesStream(data, keyMaterial);
            position += slice.Length;
            consumed += used;
        }
        return encrypted;
    }

    private static int EncryptedLength(List<int> sliceLengths) => sliceLengths.Sum() + sliceLengths.Count * SliceHeaderBytes;

    /// <summary>The data length of each slice, in order; advances <paramref name="state"/> once per possible slice, as the game does.</summary>
    private static List<int> SliceDataLengths(ref ulong state, uint dataLength)
    {
        var lengths = new List<int>();
        var laps = (dataLength >> SliceShift) + 1;
        for (uint lap = 0; lap < laps; lap++)
        {
            if (dataLength > 0)
            {
                var sliceLength = (((int)state & SliceCapacityMask) + 1) << SliceShift;
                lengths.Add(sliceLength);
                dataLength = dataLength >= (uint)sliceLength ? dataLength - (uint)sliceLength : 0;
            }
            SplitMix(ref state);
        }
        return lengths;
    }

    /// <summary>The 32 bytes of key material of the next slice (an AES-128 key, then the first block of its stream), from the state.</summary>
    private static byte[] NextKeyMaterial(ref ulong state)
    {
        var material = new byte[KeyMaterialBytes];
        var rowLength = material.Length / KeyRows;
        for (var i = 0; i < rowLength; i++)
        {
            SplitMix(ref state);
            material[i] = (byte)state;
            material[rowLength + i] = (byte)(state >> SecondRowShift);
        }
        return material;
    }

    private static void XorSplitMix(ref ulong state, Span<byte> data)
    {
        for (var i = 0; i < data.Length; i++)
        {
            SplitMix(ref state);
            data[i] ^= (byte)state;
        }
    }

    /// <summary>XORs <paramref name="data"/> (whole blocks) with the AES-128 stream of the key material: the key is its first 16 bytes,
    /// the stream starts as its last 16 and each block is the AES encryption of the one before.</summary>
    private static void XorAesStream(Span<byte> data, ReadOnlySpan<byte> keyMaterial)
    {
        using var aes = Aes.Create();
        aes.Key = keyMaterial[..AesKeyBytes].ToArray();
        var block = keyMaterial[AesKeyBytes..].ToArray();
        for (var i = 0; i < data.Length; i += AesBlockBytes)
        {
            block = aes.EncryptEcb(block, PaddingMode.None);
            for (var j = 0; j < AesBlockBytes; j++) data[i + j] ^= block[j];
        }
    }

    private static void SplitMix(ref ulong state)
    {
        state += SplitMixIncrement;
        state = (state ^ (state >> SplitMixShift1)) * SplitMixMultiplier1;
        state = (state ^ (state >> SplitMixShift2)) * SplitMixMultiplier2;
        state ^= state >> SplitMixShift3;
    }
}
