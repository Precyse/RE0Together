namespace CoopLauncher;

/// <summary>What must hold on this machine before a lobby is opened for a game, or a joined lobby's session starts: the game is
/// found, its build is supported, its required files are there and its mod is installed and current.</summary>
public static class StartChecks
{
    /// <summary>The first thing that is wrong, null when the game can start. A mod that is installed but out of date (or, once
    /// the installer reports it, modified) is installed again first while the game is not running.</summary>
    public static Refusal? Run(GameProfile profile, string? gameDir, ulong lobbyId)
    {
        Refusal Refused(RefusalKind kind, string detail = "") => new(kind, detail, profile.Id, lobbyId);
        if (gameDir == null) return Refused(RefusalKind.GameNotFound);
        var build = GameBuilds.Installed(profile, gameDir);
        if (!GameBuilds.IsSupported(profile, build)) return Refused(RefusalKind.GameUnsupported, build.ToString());
        if (profile.MissingRequirement(gameDir) is { } missing) return Refused(RefusalKind.RequirementMissing, missing);
        var mod = ModInstaller.Status(profile, gameDir);
        if (mod.State == ModState.NotInstalled) return Refused(RefusalKind.ModNotInstalled);
        if (mod.State == ModState.Installed || GameLauncher.IsRunning(profile)) return null;
        if (!ModInstaller.Install(profile, gameDir)) return Refused(RefusalKind.ModFiles);
        Log.Event($"{profile.Name} mod updated");
        return null;
    }
}
