namespace CoopLauncher;

/// <summary>What is wrong with the game process of an open session, if anything.</summary>
public enum GameCondition { Fine, Exited, Unlinked, NotStarted }

/// <summary>
/// Watches the game's process while a session is open: <see cref="GameCondition.Exited"/> once the game that was running
/// is gone (closed or crashed), <see cref="GameCondition.Unlinked"/> when it has run for a while without the adapter
/// reaching the launcher (started outside the launcher, or without the mod), <see cref="GameCondition.NotStarted"/> when
/// the launcher started the game and no process appeared. Polled by the window; the main loop is untouched.
/// </summary>
public sealed class GameWatch
{
    public const long UnlinkedAfterMs = 20_000;
    public const long NotStartedAfterMs = 60_000;

    private bool _seenRunning;
    private bool _wasRunning;
    private long _runningSinceMs;

    public GameCondition Condition { get; private set; } = GameCondition.Fine;

    /// <summary>The condition for one reading of the process: whether it runs now, whether it ever ran in this session,
    /// whether the adapter link is there (or not expected), when the launcher started the game (null when it did not) and
    /// since when the process has been running; <paramref name="unlinkedAfterMs"/> is how long it may run unlinked (the profile's).</summary>
    public static GameCondition Next(
        bool running, bool seenRunning, bool linked, long? launchedAtMs, long runningSinceMs, long nowMs, long unlinkedAfterMs = UnlinkedAfterMs)
    {
        if (!running)
        {
            if (seenRunning) return GameCondition.Exited;
            return launchedAtMs is { } launched && nowMs - launched > NotStartedAfterMs ? GameCondition.NotStarted : GameCondition.Fine;
        }
        return !linked && nowMs - runningSinceMs > unlinkedAfterMs ? GameCondition.Unlinked : GameCondition.Fine;
    }

    /// <summary>Re-reads the process. True when the condition changed.</summary>
    public bool Update(AppStatus status, GameProfile? game, long nowMs)
    {
        var next = game == null || status.LobbyId == 0 ? Reset() : Read(status, game, nowMs);
        if (next == Condition) return false;
        Condition = next;
        return true;
    }

    private GameCondition Reset()
    {
        _seenRunning = false;
        _wasRunning = false;
        return GameCondition.Fine;
    }

    /// <summary>The capture launch's game (a guest reading its character) is not the session's: it neither counts as seen running nor as
    /// closed when it ends.</summary>
    private GameCondition Read(AppStatus status, GameProfile game, long nowMs)
    {
        if (status.Capture is { State: CaptureState.Running or CaptureState.Closing }) return Reset();
        var running = GameLauncher.IsRunning(game);
        if (running && !_wasRunning) _runningSinceMs = nowMs;
        _wasRunning = running;
        _seenRunning |= running;
        return Next(running, _seenRunning, Linked(status, game), status.LaunchedAtMs, _runningSinceMs, nowMs, game.LinkTimeoutMs);
    }

    /// <summary>Whether the adapter link is there or not expected: a rendezvous game on its own transport has none, and while
    /// the lobby is being rebuilt (rejoining, waiting for the host) the held session keeps the link it had, so the game is
    /// never called unlinked for it.</summary>
    public static bool Linked(AppStatus status, GameProfile game) =>
        (game.RendezvousSession && !status.Bridged)
        || status.State is AppState.GameRunning or AppState.PeerConnected or AppState.Rejoining or AppState.WaitingForHost;
}
