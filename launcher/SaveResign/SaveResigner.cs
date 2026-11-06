using System.Globalization;

namespace CoopLauncher.SaveResign;

/// <summary>Re-signs a game's save files from one Steam account to another (profile <c>saveSync.resign</c>): the saves of games that
/// encrypt them for the account that wrote them (Dragon's Dogma 2) can then be read by a guest's game. Pure apart from
/// <see cref="ResignFile"/>.</summary>
public static class SaveResigner
{
    public const string MandarinKind = "mandarin";

    private const string HexPrefix = "0x";

    /// <summary>What is wrong with the profile's <c>resign</c> key, or null when it is usable.</summary>
    public static string? Problem(ResignProfile profile)
    {
        if (profile.Kind != MandarinKind) return $"saveSync.resign.kind '{profile.Kind}' is not supported (only '{MandarinKind}')";
        if (ParseSeed(profile.Seed) == null) return $"saveSync.resign.seed '{profile.Seed}' is not a decimal or 0x-hex 64-bit number";
        if (!MandarinUserId.IsSupported(profile.Variant)) return $"saveSync.resign.variant {profile.Variant} is not supported";
        return null;
    }

    /// <summary>The file's bytes written for <paramref name="toSteamId64"/> instead of <paramref name="fromSteamId64"/>. Throws
    /// <see cref="InvalidDataException"/> when the file is not an intact save of the first account.</summary>
    internal static byte[] Resign(ReadOnlySpan<byte> file, ResignProfile profile, ulong fromSteamId64, ulong toSteamId64)
    {
        var seed = ParseSeed(profile.Seed) ?? throw new InvalidOperationException(Problem(profile));
        var cipher = new MandarinCipher(seed);
        var plain = MandarinSaveFile.Decrypt(file, cipher, MandarinUserId.FromSteamId64(fromSteamId64, profile.Variant));
        return MandarinSaveFile.Encrypt(plain, MandarinSaveFile.FlavorOf(file), cipher, MandarinUserId.FromSteamId64(toSteamId64, profile.Variant));
    }

    /// <summary>Rewrites the file at <paramref name="path"/> for <paramref name="toSteamId64"/>; the file is left as it was when this throws
    /// <see cref="InvalidDataException"/>.</summary>
    public static void ResignFile(string path, ResignProfile profile, ulong fromSteamId64, ulong toSteamId64) =>
        File.WriteAllBytes(path, Resign(File.ReadAllBytes(path), profile, fromSteamId64, toSteamId64));

    private static ulong? ParseSeed(string text)
    {
        var isHex = text.StartsWith(HexPrefix, StringComparison.OrdinalIgnoreCase);
        var parsed = isHex
            ? ulong.TryParse(text.AsSpan(HexPrefix.Length), NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture, out var seed)
            : ulong.TryParse(text, NumberStyles.None, CultureInfo.InvariantCulture, out seed);
        return parsed ? seed : null;
    }
}
