namespace CoopLauncher;

/// <summary>A game's installed Steam build against the builds its profile lists as supported (the adapter's addresses
/// are tied to the game's executable).</summary>
public static class GameBuilds
{
    public const int Unknown = 0;

    /// <summary>The installed build id from Steam's appmanifest of that game folder; <see cref="Unknown"/> when Steam has none.</summary>
    public static int Installed(GameProfile profile, string? gameDir)
    {
        try
        {
            return SteamLibrary.InstalledBuild(profile.SteamAppId, gameDir) ?? Unknown;
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            Log.Info($"Could not read the {profile.Name} build: {e.Message}");
            return Unknown;
        }
    }

    /// <summary>False only for a known build outside a non-empty supported list.</summary>
    public static bool IsSupported(GameProfile profile, int installedBuild) =>
        profile.SupportedBuilds is not { Count: > 0 } supported || installedBuild == Unknown || supported.Contains(installedBuild);

    public static string Describe(GameProfile profile, int installedBuild) =>
        $"{profile.Name} build {installedBuild} is not supported (supported: {string.Join(", ", profile.SupportedBuilds!)})";
}
