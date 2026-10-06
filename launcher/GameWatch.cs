namespace CoopLauncher;

/// <summary>What is wrong with the game process of an open session, if anything.</summary>
public enum GameCondition { Fine, Exited, Unlinked }

/// <summary>
/// Watches the game's process while a session is open: <see cref="GameCondition.Exited"/> once the game that was running
/// is gone (closed or crashed), <see cref="GameCondition.Unlinked"/> when it has run for a while without the adapter
/// reaching the launcher (started outside the launcher, or without the mod). Polled by the window; the main loop is untouched.
/// </summary>
public sealed class GameWatch
{
    private const long UnlinkedAfterMs = 20_000;

    private bool _seenRunning;
    private bool _wasRunning;
    private long _runningSinceMs;

    public GameCondition Condition { get; private set; } = GameCondition.Fine;

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
        if (!running) return _seenRunning ? GameCondition.Exited : GameCondition.Fine;
        var linked = status.State is AppState.GameRunning or AppState.PeerConnected;
        return !linked && nowMs - _runningSinceMs > UnlinkedAfterMs ? GameCondition.Unlinked : GameCondition.Fine;
    }
}
