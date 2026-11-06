// Ported from MandarinJuice (https://github.com/mi5hmash/MandarinJuice), MIT License, Copyright (c) 2026 Michał Gębicki.
// The full licence text is in THIRD_PARTY.md.
using System.IO.Compression;
using System.Runtime.InteropServices;
using System.Security.Cryptography;

namespace CoopLauncher.SaveResign;

/// <summary>How a Mandarin save's data is stored: as is (<see cref="Default"/>) or deflate-compressed before it is encrypted
/// (<see cref="Compressible"/>). The file header names it; one game can use both for different files.</summary>
internal enum MandarinFlavor : uint
{
    Default = 0x10,
    Compressible = 0x18,
}

/// <summary>A Mandarin save file (RE Engine "DSSS"): a 16-byte header (magic, version, <see cref="MandarinFlavor"/>, reserved), the
/// data encrypted by <see cref="MandarinCipher"/>, and a 140-byte footer (128 random bytes, the length of the decrypted data, and a
/// MurmurHash3 signature of everything before it). Pure.</summary>
internal static class MandarinSaveFile
{
    private const uint Magic = 0x5353_5344;
    private const uint FormatVersion = 2;
    private const uint Reserved = 0;
    private const int HeaderWords = 4;
    private const int HeaderBytes = HeaderWords * sizeof(uint);
    private const int SaltBytes = 0x80;
    private const int FooterBytes = SaltBytes + sizeof(long) + sizeof(uint);
    private const uint SignatureSeed = 0xFFFFFFFF;
    private const int MagicWord = 0;
    private const int FormatVersionWord = 1;
    private const int FlavorWord = 2;
    private const int ReservedWord = 3;
    private const uint CompressionFormatVersion = 1;
    private const int CompressedLengthOffset = 16;
    private const int CompressionBlockHeaderBytes = 24;
    private const int CompressionSizeOverhead = 0x10;

    /// <summary>Whether <paramref name="file"/> looks like a Mandarin save (long enough, the magic, a known flavor), not that it is intact.</summary>
    public static bool IsMandarin(ReadOnlySpan<byte> file) =>
        file.Length >= HeaderBytes + FooterBytes && file.Length % sizeof(uint) == 0
        && ReadWord(file, MagicWord) == Magic && Enum.IsDefined((MandarinFlavor)ReadWord(file, FlavorWord));

    /// <summary>The plain data of a save that was written for <paramref name="userId"/>. Throws <see cref="InvalidDataException"/> when the file
    /// is not a Mandarin save, its signature is wrong, or it was written for another user id.</summary>
    public static byte[] Decrypt(ReadOnlySpan<byte> file, MandarinCipher cipher, ulong userId)
    {
        if (!IsMandarin(file)) throw new InvalidDataException("Not a Mandarin save file");
        var words = MemoryMarshal.Cast<byte, uint>(file);
        if (Murmur3.Hash(words[..^1], SignatureSeed) != words[^1]) throw new InvalidDataException("Mandarin save signature is wrong");

        var decryptedLength = MemoryMarshal.Read<long>(file[^(sizeof(long) + sizeof(uint))..]);
        var data = file[HeaderBytes..^FooterBytes];
        if (decryptedLength <= 0 || decryptedLength > data.Length) throw new InvalidDataException("Mandarin save names an impossible length");

        var plain = cipher.Decrypt(data, (int)decryptedLength, userId);
        return (MandarinFlavor)ReadWord(file, FlavorWord) == MandarinFlavor.Compressible ? Decompress(plain) : plain;
    }

    /// <summary>A save file of the given flavor holding <paramref name="plain"/> for <paramref name="userId"/>.</summary>
    public static byte[] Encrypt(ReadOnlySpan<byte> plain, MandarinFlavor flavor, MandarinCipher cipher, ulong userId)
    {
        var decrypted = flavor == MandarinFlavor.Compressible ? Compress(plain) : plain.ToArray();
        var data = cipher.Encrypt(decrypted, userId);

        var file = new byte[HeaderBytes + data.Length + FooterBytes];
        var words = MemoryMarshal.Cast<byte, uint>(file);
        words[MagicWord] = Magic;
        words[FormatVersionWord] = FormatVersion;
        words[FlavorWord] = (uint)flavor;
        words[ReservedWord] = Reserved;
        data.CopyTo(file, HeaderBytes);
        RandomNumberGenerator.Fill(file.AsSpan(HeaderBytes + data.Length, SaltBytes));
        MemoryMarshal.Write(file.AsSpan(HeaderBytes + data.Length + SaltBytes), (long)decrypted.Length);
        words[^1] = Murmur3.Hash(words[..^1], SignatureSeed);
        return file;
    }

    /// <summary>The flavor the header of a Mandarin save names.</summary>
    public static MandarinFlavor FlavorOf(ReadOnlySpan<byte> file) => (MandarinFlavor)ReadWord(file, FlavorWord);

    private static uint ReadWord(ReadOnlySpan<byte> file, int index) => MemoryMarshal.Read<uint>(file[(index * sizeof(uint))..]);

    private static byte[] Decompress(byte[] block)
    {
        if (block.Length < CompressionBlockHeaderBytes) throw new InvalidDataException("Mandarin compressed block is too short");
        var length = MemoryMarshal.Read<long>(block.AsSpan(CompressedLengthOffset));
        if (length < 0 || length > SaveReceiver.MaxFileBytes) throw new InvalidDataException("Mandarin compressed block names an impossible length");

        using var input = new MemoryStream(block, CompressionBlockHeaderBytes, block.Length - CompressionBlockHeaderBytes);
        using var deflate = new DeflateStream(input, CompressionMode.Decompress);
        var plain = new byte[length];
        try
        {
            deflate.ReadExactly(plain);
        }
        catch (EndOfStreamException)
        {
            throw new InvalidDataException("Mandarin compressed block is shorter than it says");
        }
        return plain;
    }

    private static byte[] Compress(ReadOnlySpan<byte> plain)
    {
        using var deflated = new MemoryStream();
        using (var deflate = new DeflateStream(deflated, CompressionLevel.SmallestSize, leaveOpen: true))
            deflate.Write(plain);

        using var block = new MemoryStream();
        using var writer = new BinaryWriter(block);
        writer.Write((ulong)deflated.Length + CompressionSizeOverhead);
        writer.Write(CompressionFormatVersion);
        writer.Write((uint)deflated.Length);
        writer.Write((ulong)plain.Length);
        writer.Write(deflated.GetBuffer(), 0, (int)deflated.Length);
        writer.Flush();
        return block.ToArray();
    }
}
