using System.Buffers.Binary;

namespace CoopLauncher;

/// <summary>Blowfish (Schneier 1993) in ECB mode with the byte order of the games that use it for their saves: each 8-byte block is
/// two 32-bit halves stored little-endian (the standard test vectors are the same cipher with big-endian halves). Pure.</summary>
public sealed class Blowfish
{
    public const int BlockSize = 8;

    private const int Rounds = 16;
    private const int PEntries = Rounds + 2;
    private const int SBoxEntries = 256;
    private const int ByteMask = 0xFF;
    private const int Byte1Shift = 8;
    private const int Byte2Shift = 16;
    private const int Byte3Shift = 24;

    private readonly uint[] _p = (uint[])BlowfishTables.P.Clone();
    private readonly uint[][] _s = BlowfishTables.S.Select(box => (uint[])box.Clone()).ToArray();

    public Blowfish(ReadOnlySpan<byte> key)
    {
        if (key.Length == 0) throw new ArgumentException("A Blowfish key needs at least one byte", nameof(key));
        var next = 0;
        for (var i = 0; i < PEntries; i++)
        {
            uint word = 0;
            for (var b = 0; b < sizeof(uint); b++, next = (next + 1) % key.Length) word = (word << Byte1Shift) | key[next];
            _p[i] ^= word;
        }
        uint left = 0, right = 0;
        for (var i = 0; i < PEntries; i += 2)
        {
            EncryptHalves(ref left, ref right);
            (_p[i], _p[i + 1]) = (left, right);
        }
        foreach (var box in _s)
        {
            for (var i = 0; i < SBoxEntries; i += 2)
            {
                EncryptHalves(ref left, ref right);
                (box[i], box[i + 1]) = (left, right);
            }
        }
    }

    /// <summary>One block as two halves (big-endian test vectors use these directly).</summary>
    public void EncryptHalves(ref uint left, ref uint right)
    {
        for (var i = 0; i < Rounds; i++)
        {
            left ^= _p[i];
            right ^= Mix(left);
            (left, right) = (right, left);
        }
        (left, right) = (right, left);
        right ^= _p[Rounds];
        left ^= _p[Rounds + 1];
    }

    public void DecryptHalves(ref uint left, ref uint right)
    {
        for (var i = Rounds + 1; i > 1; i--)
        {
            left ^= _p[i];
            right ^= Mix(left);
            (left, right) = (right, left);
        }
        (left, right) = (right, left);
        right ^= _p[1];
        left ^= _p[0];
    }

    /// <summary>The whole buffer decrypted block by block; its length must be a multiple of <see cref="BlockSize"/>.</summary>
    public byte[] DecryptEcb(ReadOnlySpan<byte> data) => Transform(data, DecryptHalves);

    public byte[] EncryptEcb(ReadOnlySpan<byte> data) => Transform(data, EncryptHalves);

    private delegate void HalvesOperation(ref uint left, ref uint right);

    private static byte[] Transform(ReadOnlySpan<byte> data, HalvesOperation operation)
    {
        if (data.Length % BlockSize != 0) throw new ArgumentException("Blowfish ECB needs whole 8-byte blocks", nameof(data));
        var result = new byte[data.Length];
        for (var at = 0; at < data.Length; at += BlockSize)
        {
            var left = BinaryPrimitives.ReadUInt32LittleEndian(data.Slice(at, sizeof(uint)));
            var right = BinaryPrimitives.ReadUInt32LittleEndian(data.Slice(at + sizeof(uint), sizeof(uint)));
            operation(ref left, ref right);
            BinaryPrimitives.WriteUInt32LittleEndian(result.AsSpan(at, sizeof(uint)), left);
            BinaryPrimitives.WriteUInt32LittleEndian(result.AsSpan(at + sizeof(uint), sizeof(uint)), right);
        }
        return result;
    }

    private uint Mix(uint x) =>
        ((_s[0][x >> Byte3Shift] + _s[1][(x >> Byte2Shift) & ByteMask]) ^ _s[2][(x >> Byte1Shift) & ByteMask]) + _s[3][x & ByteMask];
}
