using System.Text.Json;

namespace CoopLauncher;

/// <summary>The words of the log lines that name a component's version, one place so the updater and the mod installer say the same
/// thing: "Launcher updated to v8 (was v7)", "Saints Row 2 mod installed v12". Version 0 is a dev build and is left out.</summary>
public static class VersionLines
{
    public static string Installed(string subject, int version) => version > 0 ? $"{subject} installed v{version}" : $"{subject} installed";

    /// <summary>An update from <paramref name="was"/> (0 when the old version is not known) to <paramref name="version"/>; the same
    /// version again is a reinstall (a repair, or files that changed without a new release).</summary>
    public static string Updated(string subject, int version, int was) => (version, was) switch
    {
        (<= 0, _) => $"{subject} updated",
        (_, <= 0) => $"{subject} updated to v{version}",
        _ when version == was => $"{subject} reinstalled v{version}",
        _ => $"{subject} updated to v{version} (was v{was})",
    };

    /// <summary>What a release component is called in the log: the launcher, or a game's mod package (the game's name from its profile).</summary>
    public static string SubjectOf(string componentId) =>
        componentId == InstalledRelease.LauncherId ? "Launcher" : $"{ModName(componentId)} mod package";

    private static string ModName(string gameId)
    {
        try
        {
            return GameProfile.Load(gameId).Name;
        }
        catch (Exception e) when (e is IOException or JsonException or InvalidDataException)
        {
            return gameId;
        }
    }
}
