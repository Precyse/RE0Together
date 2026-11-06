// Ported from MandarinJuice (https://github.com/mi5hmash/MandarinJuice), MIT License, Copyright (c) 2026 Michał Gębicki.
// The full licence text is in THIRD_PARTY.md.

namespace CoopLauncher.SaveResign;

/// <summary>MurmurHash3 (32 bit) over 32-bit words, the signature in the last word of a Mandarin save file. Pure.</summary>
internal static class Murmur3
{
    private const uint BlockMultiplier1 = 0xCC9E2D51;
    private const uint BlockMultiplier2 = 0x1B873593;
    private const uint BlockAddend = 0xE6546B64;
    private const uint FinalMultiplier1 = 0x85EBCA6B;
    private const uint FinalMultiplier2 = 0xC2B2AE35;
    private const int BlockRotation1 = 15;
    private const int BlockRotation2 = 13;
    private const int BlockMixFactor = 5;
    private const int FinalShift1 = 16;
    private const int FinalShift2 = 13;

    public static uint Hash(ReadOnlySpan<uint> words, uint seed)
    {
        var hash = seed;
        foreach (var word in words)
        {
            var scrambled = BlockMultiplier2 * uint.RotateLeft(BlockMultiplier1 * word, BlockRotation1);
            hash = uint.RotateLeft(hash ^ scrambled, BlockRotation2) * BlockMixFactor + BlockAddend;
        }
        hash ^= (uint)(words.Length * sizeof(uint));
        hash ^= hash >> FinalShift1;
        hash *= FinalMultiplier1;
        hash ^= hash >> FinalShift2;
        hash *= FinalMultiplier2;
        hash ^= hash >> FinalShift1;
        return hash;
    }
}
