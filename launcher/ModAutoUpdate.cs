namespace CoopLauncher;

/// <summary>
/// Mods follow the package: an installed mod that reads <see cref="ModState.UpdateAvailable"/> (the package it came from changed) is
/// installed again while its game is closed (its log line names the version change). An installed mod whose files are the package's
/// only takes the package's mod version. A mod that is not installed stays so, a modified one waits for the player's repair, and a
/// running game is left alone. Each game is a normal install with the usual progress and failure reasons; one failing does not stop
/// the others. Runs at start and after an update that needs no restart.
/// </summary>
public static class ModAutoUpdate
{
    /// <summary>Updates every pending mod, one game at a time, and returns how many installs succeeded. <paramref name="progressFor"/>
    /// gives the sink of a game's install (null for none); <paramref name="isRunning"/> defaults to the game process check.</summary>
    public static int Run(IEnumerable<GameProfile> profiles, Func<GameProfile, string?> folderOf,
        Func<GameProfile, IProgress<ModProgress>?> progressFor, Func<GameProfile, bool>? isRunning = null)
    {
        isRunning ??= GameLauncher.IsRunning;
        var updated = 0;
        foreach (var profile in profiles)
        {
            if (folderOf(profile) is not { } gameDir || isRunning(profile)) continue;
            var state = ModInstaller.Status(profile, gameDir).State;
            if (state == ModState.Installed) RecordVersion(profile, gameDir);
            else if (state == ModState.UpdateAvailable && Install(profile, gameDir, progressFor(profile))) updated++;
        }
        return updated;
    }

    /// <summary>An installed mod that is the package's, in a release that moved its version without changing its files.</summary>
    private static void RecordVersion(GameProfile profile, string gameDir)
    {
        try
        {
            ModInstaller.RecordPackageVersion(profile, gameDir);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            Log.Info($"Could not record the {profile.Name} mod version: {e.Message}");
        }
    }

    private static bool Install(GameProfile profile, string gameDir, IProgress<ModProgress>? progress)
    {
        try
        {
            return ModInstaller.Install(profile, gameDir, progress);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            Log.Info($"Could not update the {profile.Name} mod: {e.Message}");
            progress?.Report(new ModProgress(ModStep.Failed, 0, 0, 0, e.Message));
            return false;
        }
    }
}
