// Ported from MandarinJuice (https://github.com/mi5hmash/MandarinJuice), MIT License, Copyright (c) 2026 Michał Gębicki.
// The full licence text is in THIRD_PARTY.md.
using System.Numerics;
using System.Runtime.InteropServices;

namespace CoopLauncher.SaveResign;

/// <summary>The key arithmetic of a Mandarin slice header: it holds the slice's AES key material as pairs of numbers modulo a
/// fixed modulus, and only the owner's user id recovers it. MandarinJuice does this on 2048-bit containers (<c>Limegator</c> is a
/// modular power, <c>Limeghetti</c> a division, <c>EncryptionFirst</c> a product); here it is <see cref="BigInteger"/>. Pure.</summary>
internal static class MandarinKeys
{
    /// <summary>Numbers of the key material of one slice (an AES-128 key and its first stream block).</summary>
    public const int KeyMaterialUlongs = 4;
    /// <summary>Numbers of one half of a wrapped key.</summary>
    public const int PartUlongs = 8;
    /// <summary>Numbers of the wrapped key material in a slice header.</summary>
    public const int WrappedUlongs = KeyMaterialUlongs * 2 * PartUlongs;

    private const ulong HeaderKeyExponent = 0x14;
    private const string KeyModulusBase64 = "8ztvuXKgtyUV5Fw5GCnhgq2Km9wKZNNETXnIEKuGNxc=";
    private const string UserIdModulusBase64 = "+Z23XDnQ25IKcq4cjJRwwVbFTW4FsmmipjxkiFXDmws=";
    private const string GeneratorBase64 = "5m9USvzOaMXvB7mgeyd1hTRKHbYTdugx9zufvV9E9xU=";

    private static readonly BigInteger KeyModulus = FromBase64(KeyModulusBase64);
    private static readonly BigInteger UserIdModulus = FromBase64(UserIdModulusBase64);
    private static readonly BigInteger Generator = FromBase64(GeneratorBase64);
    private static readonly BigInteger HeaderKey = PersonalKey(HeaderKeyExponent);

    /// <summary>The seed a user's keys are wrapped with: the generator to the power of the user id, then to the header exponent.</summary>
    public static BigInteger PersonalSeed(ulong userId) => BigInteger.ModPow(PersonalKey(userId), HeaderKeyExponent, KeyModulus);

    /// <summary>Wraps key material into <paramref name="wrapped"/> (<see cref="WrappedUlongs"/> numbers): per key the header key, then the key times the seed.</summary>
    public static void Wrap(Span<ulong> wrapped, ReadOnlySpan<ulong> keyMaterial, BigInteger personalSeed)
    {
        wrapped.Clear();
        for (var i = 0; i < KeyMaterialUlongs; i++)
        {
            var position = i * 2 * PartUlongs;
            WriteUlongs(HeaderKey, wrapped.Slice(position, PartUlongs));
            WriteUlongs(personalSeed * keyMaterial[i], wrapped.Slice(position + PartUlongs, PartUlongs));
        }
    }

    /// <summary>The key material in <paramref name="wrapped"/> for this user. A header that does not belong to the user gives
    /// other numbers (the caller finds out by the slice checksum), except a zero divisor, which is invalid data.</summary>
    public static void Unwrap(ReadOnlySpan<ulong> wrapped, ulong userId, Span<ulong> keyMaterial)
    {
        for (var i = 0; i < KeyMaterialUlongs; i++)
        {
            var position = i * 2 * PartUlongs;
            var divisor = BigInteger.ModPow(ReadUlongs(wrapped.Slice(position, PartUlongs)), userId, KeyModulus);
            if (divisor.IsZero) throw new InvalidDataException("Mandarin slice header holds an unusable key");
            var key = ReadUlongs(wrapped.Slice(position + PartUlongs, PartUlongs)) / divisor;
            keyMaterial[i] = (ulong)(key & ulong.MaxValue);
        }
    }

    private static BigInteger PersonalKey(ulong userId) => BigInteger.ModPow(Generator, userId % UserIdModulus, KeyModulus);

    private static BigInteger FromBase64(string text) => new(Convert.FromBase64String(text), isUnsigned: true);

    private static BigInteger ReadUlongs(ReadOnlySpan<ulong> value) => new(MemoryMarshal.AsBytes(value), isUnsigned: true);

    private static void WriteUlongs(BigInteger value, Span<ulong> destination)
    {
        destination.Clear();
        if (!value.TryWriteBytes(MemoryMarshal.AsBytes(destination), out _, isUnsigned: true))
            throw new InvalidDataException("Mandarin key does not fit its header slot");
    }
}
