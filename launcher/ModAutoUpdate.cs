namespace CoopLauncher;

/// <summary>
/// Mods follow the package: an installed mod that reads <see cref="ModState.UpdateAvailable"/> (the package it came from changed) is
/// installed again while its game is closed. A mod that is not installed stays so, a modified one waits for the player's repair, and a
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
            if (ModInstaller.Status(profile, gameDir).State != ModState.UpdateAvailable) continue;
            Log.Info($"Updating the {profile.Name} mod to the package");
            if (Install(profile, gameDir, progressFor(profile))) updated++;
        }
        return updated;
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
