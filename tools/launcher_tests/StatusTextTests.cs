using CoopLauncher;
using CoopLauncher.Gui;

namespace LauncherTests;

/// <summary>The words and lamps of the top bar, the refusals and the seats.</summary>
public static class StatusTextTests
{
    private static readonly Dictionary<RefusalKind, string> RefusalWords = new()
    {
        [RefusalKind.LauncherBuild] = "Launcher build differs",
        [RefusalKind.GameBuild] = "Game version differs",
        [RefusalKind.ModFiles] = "Mod files differ",
        [RefusalKind.ModNotInstalled] = "Mod not installed",
        [RefusalKind.GameUnsupported] = "Game build unsupported",
        [RefusalKind.GameNotFound] = "Game not found",
        [RefusalKind.RequirementMissing] = "Juiced not found",
        [RefusalKind.UnknownGame] = "Game not in this launcher",
        [RefusalKind.LobbyFull] = "Lobby full",
        [RefusalKind.LobbyClosed] = "Lobby closed",
        [RefusalKind.LobbyNotAllowed] = "Lobby join refused",
        [RefusalKind.LobbyFailed] = "Lobby join failed",
        [RefusalKind.NotCoopLobby] = "Not a co-op lobby",
        [RefusalKind.SteamOffline] = "Steam offline",
        [RefusalKind.SaveTimedOut] = "Save transfer timed out",
        [RefusalKind.SaveFolderMissing] = "Save folder not found",
        [RefusalKind.HostLeft] = "Host left",
        [RefusalKind.RejoinGaveUp] = "Could not rejoin",
        [RefusalKind.PortInUse] = "Port 27960 in use",
        [RefusalKind.LaunchFailed] = "Game did not start",
    };

    private static PlayerSlot Partner(string status = "", SessionStep step = SessionStep.None, int percent = 0, int? rtt = null, bool held = false, bool silent = false) =>
        new(2, "Partner", IsHost: false, IsLocal: false, status, step, percent, rtt, held, silent);

    private static PlayerSlot Self(SessionStep step = SessionStep.None) => new(1, "Me", IsHost: true, IsLocal: true, Step: step);

    public static void Run()
    {
        Refusals();
        Steps();
        Seats();
        TopBar();
    }

    private static void Refusals()
    {
        foreach (var kind in Enum.GetValues<RefusalKind>())
        {
            var refusal = new Refusal(kind, kind == RefusalKind.PortInUse ? "27960" : "Juiced");
            Check.That(RefusalWords.TryGetValue(kind, out var words) && StatusText.Refusal(refusal) == words, $"refusal words for {kind}");
        }
        Check.That(StatusText.Refusal(new Refusal(RefusalKind.RequirementMissing)) == "Required file not found", "a missing file without a name");
    }

    private static void Steps()
    {
        Check.That(StatusText.Step(SessionStep.None, 0) == "", "no step: no words");
        Check.That(StatusText.Step(SessionStep.Lobby, 0) == "In lobby", "lobby");
        Check.That(StatusText.Step(SessionStep.CheckingBuild, 0) == "Checking build", "checking build");
        Check.That(StatusText.Step(SessionStep.ReceivingSave, 40) == "Receiving save 40%", "receiving save");
        Check.That(StatusText.Step(SessionStep.StartingGame, 0) == "Starting game", "starting game");
        Check.That(StatusText.Step(SessionStep.InGame, 0) == "In game", "in game");
        Check.That(StatusText.Step(SessionStep.GameClosed, 0) == "Game closed", "game closed");
        Check.That(StatusText.Step(SessionStep.Refused, 0) == "Refused", "refused");
    }

    private static void Seats()
    {
        Check.That(StatusText.Seat(Partner("searching 3", SessionStep.InGame, rtt: 20, held: true, silent: true)) == "Reconnecting", "held beats everything");
        Check.That(StatusText.Seat(Partner("searching 3", SessionStep.InGame, rtt: 20, silent: true)) == "Not responding", "silent beats the game line");
        Check.That(StatusText.Seat(Partner("searching 3", SessionStep.InGame, rtt: 20)) == "searching 3", "the game's line beats the step");
        Check.That(StatusText.Seat(Partner(step: SessionStep.ReceivingSave, percent: 40, rtt: 20)) == "Receiving save 40%", "the step beats connected");
        Check.That(StatusText.Seat(Partner(rtt: 20)) == "Connected", "a peer that answers but publishes nothing is connected");
        Check.That(StatusText.Seat(Partner()) == "In lobby", "otherwise in lobby");

        Check.That(StatusText.SeatLamp(Partner(step: SessionStep.InGame)) == Theme.Live, "in game: red");
        Check.That(StatusText.SeatLamp(Partner(step: SessionStep.InGame, held: true)) == Theme.Armed, "in game but held: amber");
        Check.That(StatusText.SeatLamp(Partner(step: SessionStep.InGame, silent: true)) == Theme.Armed, "in game but silent: amber");
        Check.That(StatusText.SeatLamp(Partner(step: SessionStep.Lobby)) == Theme.Armed, "any other member: amber");
        Check.That(StatusText.SeatLamp(Partner(rtt: 10)) == Theme.Armed, "connected without a step: amber");
    }

    private static void TopBar()
    {
        Show(new AppStatus(AppState.Idle), "Idle", Theme.Dim, "idle");
        Show(new AppStatus(AppState.Offline), "Offline", Theme.Dim, "offline");
        Show(new AppStatus(AppState.Offline) { Offline = OfflineReason.SteamNotRunning }, "Steam not running", Theme.Dim, "Steam not running");
        Show(new AppStatus(AppState.Connecting), "Connecting", Theme.Armed, "connecting");
        Show(new AppStatus(AppState.Rejoining, 5) { RejoinAttempt = 2 }, "Reconnecting 2", Theme.Armed, "reconnecting with the attempt");
        Show(new AppStatus(AppState.Rejoining, 5), "Reconnecting", Theme.Armed, "reconnecting before the first attempt");
        Show(new AppStatus(AppState.WaitingForHost, 5) { SecondsLeft = 161 }, "Waiting for host 2:41", Theme.Armed, "waiting for the host");
        Show(new AppStatus(AppState.Hosting, 5), "Hosting", Theme.Armed, "hosting");
        Show(new AppStatus(AppState.Joined, 5), "Joined", Theme.Armed, "joined");
        Show(new AppStatus(AppState.GameRunning, 5), "Game running", Theme.Armed, "game running");
        Show(new AppStatus(AppState.PeerConnected, 5, 42), "Connected 42 ms", Theme.Live, "connected with the ping");
        Show(new AppStatus(AppState.PeerConnected, 5), "Connected", Theme.Live, "connected without a ping");

        Show(new AppStatus(AppState.Joined, 5) { Step = SessionStep.CheckingBuild }, "Checking build", Theme.Armed, "checking build");
        Show(new AppStatus(AppState.Joined, 5) { Step = SessionStep.ReceivingSave, SavePercent = 40 }, "Receiving save 40%", Theme.Armed, "receiving save");
        Show(new AppStatus(AppState.Joined, 5) { Step = SessionStep.StartingGame }, "Starting game", Theme.Armed, "starting game");
        Show(new AppStatus(AppState.GameRunning, 5) { Step = SessionStep.InGame }, "Game running", Theme.Armed, "in game, no partner connected yet");

        var held = new AppStatus(AppState.PeerConnected, 5, 42) { Players = [Self(), Partner(held: true)] };
        Show(held, "Partner reconnecting", Theme.Armed, "a seat held");
        var silent = new AppStatus(AppState.PeerConnected, 5, 42) { Players = [Self(), Partner(silent: true)] };
        Show(silent, "Partner not responding", Theme.Armed, "a seat silent");
        var both = new AppStatus(AppState.PeerConnected, 5, 42) { Players = [Self(), Partner(silent: true), Partner(held: true)] };
        Show(both, "Partner reconnecting", Theme.Armed, "held before silent");
        var save = held with { Step = SessionStep.ReceivingSave, SavePercent = 10 };
        Show(save, "Receiving save 10%", Theme.Armed, "the local step comes before a held seat");

        var refused = new AppStatus(AppState.Idle) { Refusal = new Refusal(RefusalKind.LobbyFull, "", "re0", 7) };
        Show(refused, "Lobby full", Theme.Armed, "a refusal replaces the state");
        Check.That(StatusText.Format(refused, GameCondition.Exited) == "Game exited", "a game problem comes before a refusal");
        Check.That(StatusText.Format(held, GameCondition.Unlinked) == "Game not linked", "a game problem comes before the state");
        Check.That(StatusText.Format(held, GameCondition.NotStarted) == "Game did not start", "game did not start");
        Check.That(StatusText.Lamp(new AppStatus(AppState.PeerConnected, 5, 42), GameCondition.Exited) == Theme.Armed, "a game problem is amber even when connected");
    }

    private static void Show(AppStatus status, string words, System.Drawing.Color lamp, string what)
    {
        Check.That(StatusText.Format(status) == words, $"top bar words: {what}");
        Check.That(StatusText.Lamp(status) == lamp, $"top bar lamp: {what}");
    }
}
