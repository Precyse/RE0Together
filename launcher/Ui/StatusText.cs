namespace CoopLauncher.Gui;

/// <summary>The words for a refused start.</summary>
public static class StatusText
{
    private const string FallbackHost = "The host";

    /// <summary><paramref name="host"/> names the host where the words do (the other side of a build difference, an unreachable host).</summary>
    public static string Refusal(Refusal refusal, string host = "") => refusal.Kind switch
    {
        RefusalKind.LauncherBuild => LauncherBuild(refusal.Detail),
        RefusalKind.GameBuild => GameBuild(refusal.Detail, host),
        RefusalKind.ModFiles => "Mod files differ",
        RefusalKind.ModNotInstalled => "Mod not installed",
        RefusalKind.GameUnsupported => GameUnsupported(refusal),
        RefusalKind.GameNotFound => "Game not found",
        RefusalKind.RequirementMissing => refusal.Detail.Length > 0 ? Requirement(refusal.Detail, otherVersion: false) : "Required file not found",
        RefusalKind.RequirementOtherVersion => Requirement(refusal.Detail, otherVersion: true),
        RefusalKind.UnknownGame => "Game not in this launcher",
        RefusalKind.LobbyFull => "Lobby full",
        RefusalKind.LobbyClosed => "Lobby closed",
        RefusalKind.LobbyNotAllowed => "Lobby join refused",
        RefusalKind.LobbyFailed => "Lobby join failed",
        RefusalKind.NotCoopLobby => "Not a co-op lobby",
        RefusalKind.SteamOffline => "Steam offline",
        RefusalKind.SaveTimedOut => "Save transfer timed out",
        RefusalKind.SaveFolderMissing => "Save folder not found",
        RefusalKind.SaveDiffers => "Save differs from host's",
        RefusalKind.HostLeft => "Host left",
        RefusalKind.Removed => "Removed from the lobby",
        RefusalKind.RejoinGaveUp => "Could not rejoin",
        RefusalKind.PortInUse => $"Port {refusal.Detail} in use",
        RefusalKind.LaunchFailed => "Game did not start",
        RefusalKind.HostUnreachable => $"Can't reach {(host.Length > 0 ? host : refusal.Detail.Length > 0 ? refusal.Detail : "the host")} through Steam",
        RefusalKind.GameRunning => $"Close {(refusal.Detail.Length > 0 ? refusal.Detail : "the game")}, then start again",
        RefusalKind.SaveResignFailed => $"{(refusal.Detail.Length > 0 ? refusal.Detail : "A save file")} could not be converted to your Steam account",
        RefusalKind.FrameworkNotLoaded => $"{(refusal.Detail.Length > 0 ? refusal.Detail : "The mod loader")} did not load",
        RefusalKind.ModNotLoaded => "The co-op mod did not load",
        RefusalKind.OtherCoopMod => $"Another co-op mod ({(refusal.Detail.Length > 0 ? refusal.Detail : "unknown")}) is installed",
        RefusalKind.ModNotStarted => refusal.Detail.Length > 0 ? $"The co-op mod could not start: {refusal.Detail}" : "The co-op mod could not start",
        RefusalKind.SaveNotProtected => "The mod could not protect your save: quit to the title",
        _ => refusal.Kind.ToString(),
    };

    /// <summary>What another player's refusal says on its card: the player is named where the words are about it.</summary>
    public static string RefusalOf(Refusal refusal, string name) => refusal.Kind switch
    {
        RefusalKind.LauncherBuild when RefusalVersions.TryParse(refusal.Detail, out var host, out var you) =>
            host > you ? $"{name} needs launcher {host}" : $"Host needs launcher {you}",
        RefusalKind.GameBuild when RefusalVersions.TryParse(refusal.Detail, out var hostBuild, out var youBuild) =>
            hostBuild > youBuild ? $"{name} has an older game version: update it in Steam" : "Your game version is older: update it in Steam",
        RefusalKind.SaveResignFailed => $"{name} could not use the save",
        _ => Refusal(refusal),
    };

    /// <summary>The host's launcher is older than this one, or this one is older than the host's (the detail is host/you).</summary>
    private static string LauncherBuild(string detail)
    {
        if (!RefusalVersions.TryParse(detail, out var host, out var you)) return "Launcher build differs";
        return host > you ? $"Your launcher is out of date (host {host}, you {you})" : $"Host needs launcher {you} (host {host}, you {you})";
    }

    /// <summary>Steam build ids only grow, so the lower one is the machine that has to update (the detail is host/you).</summary>
    private static string GameBuild(string detail, string host)
    {
        if (!RefusalVersions.TryParse(detail, out var hostBuild, out var you)) return "Game version differs";
        return hostBuild > you
            ? "Your game version is older than the host's: update it in Steam"
            : $"{(host.Length > 0 ? host : FallbackHost)} has an older game version: update it in Steam";
    }

    /// <summary>The installed Steam build is not one the mod supports (the detail is the installed build).</summary>
    private static string GameUnsupported(Refusal refusal)
    {
        if (refusal.GameId == null || !GameProfile.ListIds().Contains(refusal.GameId)) return "Game build unsupported";
        var profile = GameProfile.Load(refusal.GameId);
        var supported = profile.SupportedBuilds is { Count: > 0 } builds ? builds[^1].ToString() : string.Empty;
        var installed = refusal.Detail.Length > 0 ? $" (build {refusal.Detail})" : string.Empty;
        return supported.Length > 0 ? $"{profile.Name} was updated by Steam{installed}; this mod supports {supported}" : "Game build unsupported";
    }

    /// <summary>The words for a required file the game folder does not satisfy: absent, or another version than the accepted ones.</summary>
    public static string Requirement(string name, bool otherVersion) => otherVersion ? $"{name} is another version" : $"{name} not found";
}
