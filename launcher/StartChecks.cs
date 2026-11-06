namespace CoopLauncher;

/// <summary>What must hold on this machine before a lobby is opened for a game, or a joined lobby's session starts: the game is
/// found and, when it reads its settings only at start, not running, its build is supported, its required files are there and its mod is installed and current. A conflicting mod
/// (<see cref="ConflictArchive"/>) is moved to the archive first while the game is closed.</summary>
public static class StartChecks
{
    /// <summary>The first thing that is wrong, null when the game can start. A mod that is installed but out of date (or, once
    /// the installer reports it, modified) is installed again first while the game is not running.</summary>
    public static Refusal? Run(GameProfile profile, string? gameDir, ulong lobbyId)
    {
        Refusal Refused(RefusalKind kind, string detail = "") => new(kind, detail, profile.Id, lobbyId);
        if (gameDir == null) return Refused(RefusalKind.GameNotFound);
        if (profile.SettingsAtBoot && GameLauncher.IsRunning(profile)) return Refused(RefusalKind.GameRunning, profile.Name);
        var build = GameBuilds.Installed(profile, gameDir);
        if (!GameBuilds.IsSupported(profile, build)) return Refused(RefusalKind.GameUnsupported, build.ToString());
        ConflictArchive.Run(profile, gameDir, GameLauncher.IsRunning(profile), DateTime.Now);
        if (profile.UnmetRequirement(gameDir) is { } unmet)
            return Refused(unmet.OtherVersion ? RefusalKind.RequirementOtherVersion : RefusalKind.RequirementMissing, unmet.Name);
        var mod = ModInstaller.Status(profile, gameDir);
        if (mod.State == ModState.NotInstalled) return Refused(RefusalKind.ModNotInstalled);
        if (mod.State == ModState.Installed || GameLauncher.IsRunning(profile)) return null;
        if (!ModInstaller.Install(profile, gameDir)) return Refused(RefusalKind.ModFiles);
        Log.Event($"{profile.Name} mod updated");
        return null;
    }
}
