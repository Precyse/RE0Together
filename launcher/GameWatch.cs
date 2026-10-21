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
    /// since when the process has been running.</summary>
    public static GameCondition Next(bool running, bool seenRunning, bool linked, long? launchedAtMs, long runningSinceMs, long nowMs)
    {
        if (!running)
        {
            if (seenRunning) return GameCondition.Exited;
            return launchedAtMs is { } launched && nowMs - launched > NotStartedAfterMs ? GameCondition.NotStarted : GameCondition.Fine;
        }
        return !linked && nowMs - runningSinceMs > UnlinkedAfterMs ? GameCondition.Unlinked : GameCondition.Fine;
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

    private GameCondition Read(AppStatus status, GameProfile game, long nowMs)
    {
        var running = GameLauncher.IsRunning(game);
        if (running && !_wasRunning) _runningSinceMs = nowMs;
        _wasRunning = running;
        _seenRunning |= running;
        var linked = (game.RendezvousSession && !status.Bridged) || status.State is AppState.GameRunning or AppState.PeerConnected;
        return Next(running, _seenRunning, linked, status.LaunchedAtMs, _runningSinceMs, nowMs);
    }
}
