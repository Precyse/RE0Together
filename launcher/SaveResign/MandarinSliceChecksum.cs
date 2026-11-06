// Ported from MandarinJuice (https://github.com/mi5hmash/MandarinJuice), MIT License, Copyright (c) 2026 Michał Gębicki.
// The full licence text is in THIRD_PARTY.md.
using System.Buffers.Binary;
using System.Runtime.InteropServices;

namespace CoopLauncher.SaveResign;

/// <summary>The 64-bit checksum of a slice's data that a Mandarin slice header holds (a hash with the constants of CityHash64 v1.0,
/// one routine per length class). Pure.</summary>
internal static class MandarinSliceChecksum
{
    private const ulong K0 = 0xC3A5C85C97CB3127;
    private const ulong K1 = 0xB492B66FBE98F273;
    private const ulong K2 = 0x9AE16A3B2F90404F;
    private const ulong K2Shifted = 0xE16A3B2F90404F00;
    private const ulong Mul = 0x9DDFEA08EB382D69;
    private const ulong SubtractedFrom8To16 = 0x651E95C4D06FBFB1;
    private const ulong Mul33To64 = 9;
    private const int Shift = 47;
    private const int Rot18 = 18;
    private const int Rot20 = 20;
    private const int Rot21 = 21;
    private const int Rot22 = 22;
    private const int Rot25 = 25;
    private const int Rot27 = 27;
    private const int Rot30 = 30;
    private const int Rot31 = 31;
    private const int UlongsPerBatch = 8;
    private const int BatchBytes = UlongsPerBatch * sizeof(ulong);
    private const int Max32 = 32;
    private const int Max16 = 16;
    private const int Min8 = 8;
    private const int Min4 = 4;
    private const int LastByteWeight = 4;
    private const int FirstWordWeight = 8;

    public static ulong Of(ReadOnlySpan<byte> data)
    {
        var length = data.Length;
        if (length > BatchBytes) return Over64(data);
        if (length > Max32) return From33To64(data);
        if (length > Max16) return From17To32(data);
        if (length >= Min8) return From8To16(data);
        if (length >= Min4) return From4To7(data);
        return length > 0 ? From1To3(data) : K2;
    }

    private static ulong Over64(ReadOnlySpan<byte> data)
    {
        var length = (ulong)data.Length;
        var words = MemoryMarshal.Cast<byte, ulong>(data);
        var last = MemoryMarshal.Cast<byte, ulong>(data[^BatchBytes..]);

        var xor0 = (last[^6] + length) ^ last[^3];
        var xor = ((xor0 * Mul) >> Shift) ^ (xor0 * Mul) ^ last[^3];
        var a = (((xor * Mul) >> Shift) ^ (xor * Mul)) * Mul;

        var sum0 = length + last[^8];
        var b = sum0 + ulong.RotateLeft(sum0 + last[^7] + last[^6], Rot20) + ulong.RotateRight(sum0 + last[^5] + a, Rot21);

        sum0 = K1 + last[^7] + last[^4] + last[^2];
        var c = sum0 + ulong.RotateLeft(sum0 + last[^3] + last[^2], Rot20) + ulong.RotateRight(sum0 + last[^5] + last[^1], Rot21);
        var d = sum0 + last[^3] + last[^2] + last[^1];
        var e = length + last[^8] + last[^7] + last[^6] + last[^5];
        var f = K1 * last[^5] + words[0];
        var g = last[^7] + last[^2];
        ulong h = 0;

        var batches = (data.Length - 1) / BatchBytes;
        for (var i = 0; i < batches; i++)
        {
            var w = words[(i * UlongsPerBatch)..];

            h = (K1 * ulong.RotateLeft(e + f + g + w[1], Rot27)) ^ c;
            g = K1 * ulong.RotateLeft(b + g + w[6], Rot22) + w[5] + e;
            f = K1 * ulong.RotateLeft(a + d, Rot31);

            var mul = K1 * b;
            var sum = w[2] + w[1] + w[0];
            e = mul + w[3] + sum;
            b = mul + ulong.RotateLeft(mul + sum, Rot20)
                + ulong.RotateRight(mul + w[3] + w[0] + d + h, Rot21) + w[0];

            c += w[4];
            sum0 = c + f;
            d = w[7] + w[6] + w[5] + sum0;
            c = ulong.RotateLeft(w[6] + w[5] + sum0, Rot20) + ulong.RotateRight(w[7] + w[2] + sum0 + g, Rot21) + sum0;
            a = h;
        }

        a = (b ^ c) * Mul;
        b = ((a >> Shift) ^ a ^ c) * Mul;
        a = (d ^ e) * Mul;
        c = ((a >> Shift) ^ a ^ d) * Mul;
        a = ((g >> Shift) ^ g) * K1 + h;
        d = ((b >> Shift) ^ b) * Mul + f;
        b = ((((c >> Shift) ^ c) * Mul + a) ^ d) * Mul;
        a = ((b >> Shift) ^ b ^ d) * Mul;
        return ((a >> Shift) ^ a) * Mul;
    }

    private static ulong From33To64(ReadOnlySpan<byte> data)
    {
        var words = MemoryMarshal.Cast<byte, ulong>(data);
        var last = MemoryMarshal.Cast<byte, ulong>(data[^Max32..]);
        var mul = (ulong)data.Length * 2 + K2;

        var a = words[3] * Mul33To64 + K2 * words[2];
        var b = ulong.RotateLeft(a, Rot22) + last[^3];
        var c = a + last[^3];
        var d = K2 * words[0] + last[^1];
        var e = words[3] * Mul33To64 + (d ^ last[^4]) + 1;

        var swapped = BinaryPrimitives.ReverseEndianness(
            mul * (ulong.RotateLeft(d, Rot21) + (ulong.RotateRight(words[1], Rot30) + last[^3]) * Mul33To64 + e));
        swapped = BinaryPrimitives.ReverseEndianness(mul * (swapped + e + mul * last[^2]));
        swapped = BinaryPrimitives.ReverseEndianness(mul * (swapped + b + c + last[^1]));
        swapped = mul * (swapped + c + words[1]) + mul * last[^2] + last[^4];
        return mul * ((swapped >> Shift) ^ swapped) + b;
    }

    private static ulong From17To32(ReadOnlySpan<byte> data)
    {
        var words = MemoryMarshal.Cast<byte, ulong>(data);
        var last = MemoryMarshal.Cast<byte, ulong>(data[^Max16..]);
        var mul = (ulong)data.Length * 2 + K2;

        var a = ulong.RotateRight(words[1] + K2, Rot18) + mul * last[^1] + words[0] * K1;
        var b = mul * ((ulong.RotateLeft(words[1] + words[0] * K1, Rot21) + ulong.RotateRight(mul * last[^1], Rot30) + K2 * last[^2]) ^ a);
        var c = mul * ((b >> Shift) ^ a ^ b);
        return ((c >> Shift) ^ c) * mul;
    }

    private static ulong From8To16(ReadOnlySpan<byte> data)
    {
        var first = BinaryPrimitives.ReadUInt64LittleEndian(data);
        var last = BinaryPrimitives.ReadUInt64LittleEndian(data[^Min8..]);
        var mul = (ulong)data.Length * 2 + K2;

        var a = first - SubtractedFrom8To16;
        var b = mul * (ulong.RotateRight(a, Rot25) + last);
        var c = mul * ((mul * ulong.RotateLeft(last, Rot27) + a) ^ b);
        var d = mul * ((c >> Shift) ^ c ^ b);
        return ((d >> Shift) ^ d) * mul;
    }

    private static ulong From4To7(ReadOnlySpan<byte> data)
    {
        var first = BinaryPrimitives.ReadUInt32LittleEndian(data);
        var last = BinaryPrimitives.ReadUInt32LittleEndian(data[^Min4..]);
        var mul = (ulong)data.Length * 2 + K2;

        var a = mul * ((first * (ulong)FirstWordWeight + (ulong)data.Length) ^ last);
        var b = mul * ((a >> Shift) ^ a ^ last);
        return mul * ((b >> Shift) ^ b);
    }

    private static ulong From1To3(ReadOnlySpan<byte> data)
    {
        var length = (ulong)data.Length;
        var a = (K2Shifted * data[data.Length / 2] + K2 * data[0])
                ^ (K0 * (length + (ulong)data[^1] * LastByteWeight));
        return ((a >> Shift) ^ a) * K2;
    }
}
