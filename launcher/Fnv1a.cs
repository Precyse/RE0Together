namespace CoopLauncher;

/// <summary>FNV-1a, 32 bit: the one hash of the launcher for cache keys and for a save slot's identity (the adapter computes the same
/// over the same bytes).</summary>
public static class Fnv1a
{
    private const uint Offset = 2166136261;
    private const uint Prime = 16777619;

    public static uint Hash(ReadOnlySpan<byte> data)
    {
        var hash = Offset;
        foreach (var next in data) hash = (hash ^ next) * Prime;
        return hash;
    }
}
