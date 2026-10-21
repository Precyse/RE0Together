using CoopLauncher;

namespace LauncherTests;

/// <summary>GameWatch.Next: the decision about the game process, with the clock and the readings given.</summary>
public static class GameWatchTests
{
    private const long Now = 500_000;

    public static void Run()
    {
        Check.That(Next(running: true, seen: true, linked: true) == GameCondition.Fine, "running and linked: fine");
        Check.That(Next(running: true, seen: true, linked: false, runningSince: Now - GameWatch.UnlinkedAfterMs) == GameCondition.Fine, "not linked yet, inside the wait: fine");
        Check.That(Next(running: true, seen: true, linked: false, runningSince: Now - GameWatch.UnlinkedAfterMs - 1) == GameCondition.Unlinked, "running unlinked past the wait");
        Check.That(Next(running: false, seen: true, linked: true) == GameCondition.Exited, "was running, now gone: exited");
        Check.That(Next(running: false, seen: true, linked: true, launched: Now - 1) == GameCondition.Exited, "exited wins over not started");
        Check.That(Next(running: false, seen: false, linked: false) == GameCondition.Fine, "never launched, never seen: fine");
        Check.That(Next(running: false, seen: false, linked: false, launched: Now - GameWatch.NotStartedAfterMs) == GameCondition.Fine, "launched, inside the wait: fine");
        Check.That(Next(running: false, seen: false, linked: false, launched: Now - GameWatch.NotStartedAfterMs - 1) == GameCondition.NotStarted, "launched and nothing appeared in time: not started");
        Check.That(Next(running: true, seen: true, linked: true, launched: Now - 10 * GameWatch.NotStartedAfterMs) == GameCondition.Fine, "launched long ago and running: fine");

        var lobbyGame = new GameProfile("g", "G", 1, "g.exe", 2, 1, [], "");
        var rendezvousGame = lobbyGame with { SessionKind = SessionKind.Rendezvous };
        Check.That(GameWatch.Linked(new AppStatus(AppState.GameRunning), lobbyGame), "game running: linked");
        Check.That(GameWatch.Linked(new AppStatus(AppState.PeerConnected), lobbyGame), "peer connected: linked");
        Check.That(GameWatch.Linked(new AppStatus(AppState.Rejoining), lobbyGame), "rejoining a held session: not judged unlinked");
        Check.That(GameWatch.Linked(new AppStatus(AppState.WaitingForHost), lobbyGame), "waiting for the host: not judged unlinked");
        Check.That(!GameWatch.Linked(new AppStatus(AppState.Joined), lobbyGame), "joined without the adapter: unlinked");
        Check.That(GameWatch.Linked(new AppStatus(AppState.Joined), rendezvousGame), "rendezvous game on its own transport: no link expected");
        Check.That(!GameWatch.Linked(new AppStatus(AppState.Joined) { Bridged = true }, rendezvousGame), "bridged rendezvous game: link expected");
    }

    private static GameCondition Next(bool running, bool seen, bool linked, long? launched = null, long runningSince = Now) =>
        GameWatch.Next(running, seen, linked, launched, runningSince, Now);
}
