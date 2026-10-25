namespace CoopLauncher.Gui;

/// <summary>The words for a refused start.</summary>
public static class StatusText
{
    public static string Refusal(Refusal refusal) => refusal.Kind switch
    {
        RefusalKind.LauncherBuild => "Launcher build differs",
        RefusalKind.GameBuild => "Game version differs",
        RefusalKind.ModFiles => "Mod files differ",
        RefusalKind.ModNotInstalled => "Mod not installed",
        RefusalKind.GameUnsupported => "Game build unsupported",
        RefusalKind.GameNotFound => "Game not found",
        RefusalKind.RequirementMissing => refusal.Detail.Length > 0 ? $"{refusal.Detail} not found" : "Required file not found",
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
        _ => refusal.Kind.ToString(),
    };
}
