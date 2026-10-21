namespace CoopLauncher.Gui;

/// <summary>The words and lamp colours for the app's state (top bar), for a refused start and for each player's seat.</summary>
public static class StatusText
{
    private const string TimeLeftFormat = @"m\:ss";

    private readonly record struct Display(string Words, Color Lamp);

    /// <summary>The top bar's words, first that applies: a problem with the game process, a refusal, the state. In a session the
    /// local step (checking build, receiving save, starting game) and a partner that is held or silent come before the state.</summary>
    public static string Format(AppStatus status, GameCondition condition = GameCondition.Fine) => Describe(status, condition).Words;

    /// <summary>Red only while connected; amber for everything in progress, held, silent, refused or wrong; grey at rest.</summary>
    internal static Color Lamp(AppStatus status, GameCondition condition = GameCondition.Fine) => Describe(status, condition).Lamp;

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
        RefusalKind.HostLeft => "Host left",
        RefusalKind.RejoinGaveUp => "Could not rejoin",
        RefusalKind.PortInUse => $"Port {refusal.Detail} in use",
        RefusalKind.LaunchFailed => "Game did not start",
        _ => refusal.Kind.ToString(),
    };

    public static string Step(SessionStep step, int percent) => step switch
    {
        SessionStep.Lobby => "In lobby",
        SessionStep.CheckingBuild => "Checking build",
        SessionStep.ReceivingSave => $"Receiving save {percent}%",
        SessionStep.StartingGame => "Starting game",
        SessionStep.InGame => "In game",
        SessionStep.GameClosed => "Game closed",
        SessionStep.Refused => "Refused",
        _ => string.Empty,
    };

    /// <summary>A seat's words, first that applies: held, silent, the game's own line, the published step, connected (a peer
    /// that answers pings but publishes no step), in lobby.</summary>
    public static string Seat(PlayerSlot player)
    {
        if (player.Held) return "Reconnecting";
        if (player.Silent) return "Not responding";
        if (player.Status.Length > 0) return player.Status;
        if (player.Step != SessionStep.None) return Step(player.Step, player.Percent);
        return player.RttMs != null ? "Connected" : "In lobby";
    }

    /// <summary>Red for a member who is in game and neither held nor silent, amber for every other member.</summary>
    internal static Color SeatLamp(PlayerSlot player) =>
        player.Step == SessionStep.InGame && !player.Held && !player.Silent ? Theme.Live : Theme.Armed;

    private static Display Describe(AppStatus status, GameCondition condition) => condition switch
    {
        GameCondition.Exited => new Display("Game exited", Theme.Armed),
        GameCondition.Unlinked => new Display("Game not linked", Theme.Armed),
        GameCondition.NotStarted => new Display("Game did not start", Theme.Armed),
        _ when status.Refusal is { } refusal => new Display(Refusal(refusal), Theme.Armed),
        _ => DescribeState(status),
    };

    private static Display DescribeState(AppStatus status)
    {
        var partners = status.Players.Where(player => !player.IsLocal).ToList();
        return status.State switch
        {
            AppState.Idle => new Display("Idle", Theme.Dim),
            AppState.Offline => new Display(status.Offline == OfflineReason.SteamNotRunning ? "Steam not running" : "Offline", Theme.Dim),
            AppState.Connecting => new Display("Connecting", Theme.Armed),
            AppState.Rejoining => new Display(status.RejoinAttempt > 0 ? $"Reconnecting {status.RejoinAttempt}" : "Reconnecting", Theme.Armed),
            AppState.WaitingForHost => new Display($"Waiting for host {TimeSpan.FromSeconds(status.SecondsLeft).ToString(TimeLeftFormat)}", Theme.Armed),
            _ when status.Step is SessionStep.CheckingBuild or SessionStep.ReceivingSave or SessionStep.StartingGame =>
                new Display(Step(status.Step, status.SavePercent), Theme.Armed),
            _ when partners.Any(player => player.Held) => new Display("Partner reconnecting", Theme.Armed),
            _ when partners.Any(player => player.Silent) => new Display("Partner not responding", Theme.Armed),
            AppState.Hosting => new Display("Hosting", Theme.Armed),
            AppState.Joined => new Display("Joined", Theme.Armed),
            AppState.GameRunning => new Display("Game running", Theme.Armed),
            _ => new Display(status.RttMs is { } rtt ? $"Connected {rtt} ms" : "Connected", Theme.Live),
        };
    }
}
