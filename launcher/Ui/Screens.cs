namespace CoopLauncher.Gui;

/// <summary>The screens under the top bar. Home and Lobbies are the two tabs; Joining, Hosting and Lobby are the session
/// screens; Settings and Select are the views that cover the whole area.</summary>
internal enum ScreenKind { Home, Lobbies, Joining, Hosting, Lobby, Settings, Select }

/// <summary>The top bar's tabs, in order.</summary>
internal enum Tab { Play, Lobbies }

/// <summary>What the player last asked for: HOST, or a join (typed code, friend card, lobby row or popup).</summary>
internal enum Intent { None, Host, Join }

/// <summary>How the window moves from one screen to the next: nothing, the page-side-by-side slide (Direction 1 comes in from
/// the right), or the panel reveal in (a screen opening over another) or out (the reverse).</summary>
internal enum TransitionKind { None, Page, PanelIn, PanelOut }

internal readonly record struct Transition(TransitionKind Kind, int Direction = 0);

/// <summary>The pure rules for which screen the window shows and how it moves between two; unit tested
/// (<c>ScreensTests</c>).</summary>
internal static class Screens
{
    private const int SessionDepth = 1;
    private const int LobbyDepth = 2;
    private const int ViewDepth = 3;

    /// <summary>Which screen the status asks for. Idle and offline show the tab, except that an idle status holding a refusal
    /// for the lobby the player asked to join keeps the joining screen (its failed step and its fix) until Cancel. Connecting
    /// shows hosting after HOST and joining otherwise. Rejoining and waiting for the host show the lobby. In a lobby the host sees
    /// hosting until its game is in; a guest sees joining through its join steps (and for a refusal's fix), then hosting while it
    /// waits for the host and while its game starts; once the game is in (or closed) it is the lobby.</summary>
    public static ScreenKind For(AppStatus status, Intent intent, JoinTarget? target, Tab tab)
    {
        switch (status.State)
        {
            case AppState.Idle or AppState.Offline:
                return RefusedJoin(status, intent, target) ? ScreenKind.Joining : TabScreen(tab);
            case AppState.Connecting:
                return intent == Intent.Host ? ScreenKind.Hosting : ScreenKind.Joining;
            case AppState.Rejoining or AppState.WaitingForHost:
                return ScreenKind.Lobby;
            default:
                var local = status.Players.FirstOrDefault(player => player.IsLocal);
                var host = local?.IsHost ?? (status.State == AppState.Hosting || intent == Intent.Host);
                if (status.Step is SessionStep.InGame or SessionStep.GameClosed) return ScreenKind.Lobby;
                return host || local != null && JoinedWaiting(status) ? ScreenKind.Hosting : ScreenKind.Joining;
        }
    }

    /// <summary>The motion between two screens: the tabs slide sideways in tab order; a screen that opens over a shallower one is
    /// revealed in and the reverse when it goes; joining or hosting moving on to the lobby slides forward.</summary>
    public static Transition Between(ScreenKind from, ScreenKind to)
    {
        if (from == to) return new Transition(TransitionKind.None);
        if (IsTab(from) && IsTab(to)) return new Transition(TransitionKind.Page, to == ScreenKind.Lobbies ? 1 : -1);
        var (before, after) = (Depth(from), Depth(to));
        if (before == after || before == SessionDepth && after == LobbyDepth) return new Transition(TransitionKind.Page, 1);
        return new Transition(after > before ? TransitionKind.PanelIn : TransitionKind.PanelOut);
    }

    /// <summary>A guest that has not been refused and is through its join steps (build check, save): it waits for the host and
    /// stays through the launch on the hosting screen.</summary>
    private static bool JoinedWaiting(AppStatus status) =>
        status.Refusal == null && status.Step is SessionStep.Ready or SessionStep.StartingGame;

    private static bool RefusedJoin(AppStatus status, Intent intent, JoinTarget? target) =>
        intent == Intent.Join && target != null && status.Refusal is { LobbyId: not 0 } refusal && refusal.LobbyId == target.LobbyId;

    private static ScreenKind TabScreen(Tab tab) => tab == Tab.Lobbies ? ScreenKind.Lobbies : ScreenKind.Home;

    private static bool IsTab(ScreenKind kind) => kind is ScreenKind.Home or ScreenKind.Lobbies;

    /// <summary>How far a screen sits above the tabs: the tabs, joining and hosting, the lobby, the views.</summary>
    private static int Depth(ScreenKind kind) => kind switch
    {
        ScreenKind.Home or ScreenKind.Lobbies => 0,
        ScreenKind.Joining or ScreenKind.Hosting => SessionDepth,
        ScreenKind.Lobby => LobbyDepth,
        _ => ViewDepth,
    };
}
