using CoopLauncher;
using CoopLauncher.Gui;

namespace LauncherTests;

/// <summary>Which screen the status asks for, and how the window moves between two screens.</summary>
public static class ScreensTests
{
    private const ulong LobbyId = 109775243712345678;

    private static readonly JoinTarget Target = new(LobbyId, "Amy", 7);

    private static PlayerSlot Me(bool host, SessionStep step = SessionStep.Lobby) => new(1, "Me", IsHost: host, IsLocal: true, Step: step);

    private static AppStatus InLobby(AppState state, bool host, SessionStep step) =>
        new(state, LobbyId) { GameId = "re0", Step = step, Players = [Me(host, step)] };

    public static void Run()
    {
        Idle();
        Connecting();
        InSession();
        Moves();
    }

    private static void Idle()
    {
        Check.That(Screens.For(AppStatus.Idle, Intent.None, null, Tab.Play) == ScreenKind.Home, "screen: idle shows the Play tab");
        Check.That(Screens.For(AppStatus.Idle, Intent.None, null, Tab.Lobbies) == ScreenKind.Lobbies, "screen: idle shows the Lobbies tab");
        Check.That(Screens.For(new AppStatus(AppState.Offline), Intent.None, null, Tab.Play) == ScreenKind.Home, "screen: offline shows the tab");

        var refused = AppStatus.Idle with { Refusal = new Refusal(RefusalKind.ModFiles, "412/409", "re0", LobbyId) };
        Check.That(Screens.For(refused, Intent.Join, Target, Tab.Play) == ScreenKind.Joining, "screen: a refusal for the joined lobby keeps the joining screen");
        Check.That(Screens.For(refused, Intent.Join, Target, Tab.Lobbies) == ScreenKind.Joining, "screen: also from the Lobbies tab");
        Check.That(Screens.For(refused, Intent.Host, null, Tab.Play) == ScreenKind.Home, "screen: a refusal after HOST shows on home");
        Check.That(Screens.For(refused, Intent.None, null, Tab.Play) == ScreenKind.Home, "screen: no intent, no joining screen");
        var other = AppStatus.Idle with { Refusal = new Refusal(RefusalKind.ModFiles, "", "re0", LobbyId + 1) };
        Check.That(Screens.For(other, Intent.Join, Target, Tab.Play) == ScreenKind.Home, "screen: a refusal of another lobby shows on home");
        var unbound = AppStatus.Idle with { Refusal = new Refusal(RefusalKind.GameNotFound, "", "re0", 0) };
        Check.That(Screens.For(unbound, Intent.Join, Target, Tab.Play) == ScreenKind.Home, "screen: a refusal without a lobby shows on home");
    }

    private static void Connecting()
    {
        var connecting = new AppStatus(AppState.Connecting);
        Check.That(Screens.For(connecting, Intent.Host, null, Tab.Play) == ScreenKind.Hosting, "screen: connecting after HOST shows hosting");
        Check.That(Screens.For(connecting, Intent.Join, Target, Tab.Play) == ScreenKind.Joining, "screen: connecting after a join shows joining");
        Check.That(Screens.For(connecting, Intent.None, null, Tab.Play) == ScreenKind.Joining, "screen: connecting without an intent shows joining");
    }

    private static void InSession()
    {
        Check.That(Screens.For(new AppStatus(AppState.Rejoining, LobbyId), Intent.Join, Target, Tab.Play) == ScreenKind.Lobby, "screen: rejoining shows the lobby");
        Check.That(Screens.For(new AppStatus(AppState.WaitingForHost, LobbyId), Intent.Host, null, Tab.Play) == ScreenKind.Lobby, "screen: waiting for the host shows the lobby");

        Check.That(Screens.For(InLobby(AppState.Hosting, host: true, SessionStep.Ready), Intent.Host, null, Tab.Play) == ScreenKind.Hosting, "screen: a host before its game is in hosting");
        Check.That(Screens.For(InLobby(AppState.PeerConnected, host: true, SessionStep.StartingGame), Intent.Host, null, Tab.Play) == ScreenKind.Hosting, "screen: the host starting its game is still hosting");
        Check.That(Screens.For(InLobby(AppState.PeerConnected, host: true, SessionStep.InGame), Intent.Host, null, Tab.Play) == ScreenKind.Lobby, "screen: the host in game is in the lobby");
        Check.That(Screens.For(InLobby(AppState.PeerConnected, host: true, SessionStep.GameClosed), Intent.Host, null, Tab.Play) == ScreenKind.Lobby, "screen: the host's game closed is the lobby");

        foreach (var step in new[] { SessionStep.Lobby, SessionStep.CheckingBuild, SessionStep.ReceivingSave })
            Check.That(Screens.For(InLobby(AppState.Joined, host: false, step), Intent.Join, Target, Tab.Play) == ScreenKind.Joining, $"screen: a guest at {step} is joining");
        foreach (var step in new[] { SessionStep.Ready, SessionStep.StartingGame })
            Check.That(Screens.For(InLobby(AppState.Joined, host: false, step), Intent.Join, Target, Tab.Play) == ScreenKind.Hosting, $"screen: a guest at {step} stays on the hosting screen");
        var refusal = new Refusal(RefusalKind.ModFiles, "412/409", "re0", LobbyId);
        Check.That(Screens.For(InLobby(AppState.Joined, host: false, SessionStep.Refused) with { Refusal = refusal }, Intent.Join, Target, Tab.Play) == ScreenKind.Joining,
            "screen: a guest that was refused keeps joining for its fix");
        Check.That(Screens.For(InLobby(AppState.Joined, host: false, SessionStep.Lobby) with { Refusal = refusal }, Intent.Join, Target, Tab.Play) == ScreenKind.Joining,
            "screen: a refusal alone keeps joining");
        Check.That(Screens.For(InLobby(AppState.PeerConnected, host: false, SessionStep.InGame), Intent.Join, Target, Tab.Play) == ScreenKind.Lobby, "screen: a guest in game is in the lobby");
        Check.That(Screens.For(InLobby(AppState.PeerConnected, host: false, SessionStep.GameClosed), Intent.Join, Target, Tab.Play) == ScreenKind.Lobby, "screen: a guest's game closed is the lobby");

        var unknown = new AppStatus(AppState.Hosting, LobbyId) { GameId = "re0", Step = SessionStep.Ready };
        Check.That(Screens.For(unknown, Intent.Host, null, Tab.Play) == ScreenKind.Hosting, "screen: no local member yet, hosting follows the state");
        Check.That(Screens.For(new AppStatus(AppState.Joined, LobbyId), Intent.Join, Target, Tab.Play) == ScreenKind.Joining, "screen: no local member yet, joined is joining");
    }

    private static void Moves()
    {
        static Transition Move(ScreenKind from, ScreenKind to) => Screens.Between(from, to);

        Check.That(Move(ScreenKind.Home, ScreenKind.Home).Kind == TransitionKind.None, "motion: no move to the same screen");
        Check.That(Move(ScreenKind.Home, ScreenKind.Lobbies) == new Transition(TransitionKind.Page, 1), "motion: Play to Lobbies slides from the right");
        Check.That(Move(ScreenKind.Lobbies, ScreenKind.Home) == new Transition(TransitionKind.Page, -1), "motion: Lobbies to Play slides from the left");
        Check.That(Move(ScreenKind.Home, ScreenKind.Joining).Kind == TransitionKind.PanelIn && Move(ScreenKind.Lobbies, ScreenKind.Hosting).Kind == TransitionKind.PanelIn,
            "motion: joining and hosting open with the panel reveal");
        Check.That(Move(ScreenKind.Joining, ScreenKind.Home).Kind == TransitionKind.PanelOut && Move(ScreenKind.Hosting, ScreenKind.Lobbies).Kind == TransitionKind.PanelOut,
            "motion: and close with its reverse");
        Check.That(Move(ScreenKind.Joining, ScreenKind.Lobby) == new Transition(TransitionKind.Page, 1) && Move(ScreenKind.Hosting, ScreenKind.Lobby) == new Transition(TransitionKind.Page, 1),
            "motion: joining or hosting to the lobby slides forward");
        Check.That(Move(ScreenKind.Lobby, ScreenKind.Home).Kind == TransitionKind.PanelOut, "motion: the lobby closes to a tab with the reverse reveal");
        Check.That(Move(ScreenKind.Home, ScreenKind.Lobby).Kind == TransitionKind.PanelIn, "motion: a lobby rebuilt from a tab opens with the reveal");
        Check.That(Move(ScreenKind.Home, ScreenKind.Settings).Kind == TransitionKind.PanelIn && Move(ScreenKind.Hosting, ScreenKind.Select).Kind == TransitionKind.PanelIn,
            "motion: the views open with the reveal");
        Check.That(Move(ScreenKind.Settings, ScreenKind.Home).Kind == TransitionKind.PanelOut && Move(ScreenKind.Select, ScreenKind.Lobby).Kind == TransitionKind.PanelOut,
            "motion: and close with its reverse");
    }
}
