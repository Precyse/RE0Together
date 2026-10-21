namespace CoopLauncher;

/// <summary>When retries are due and when to give up. Pure: the caller supplies the clock. The first attempt is due one
/// interval after the start.</summary>
public sealed class RetrySchedule
{
    private const long MsPerSecond = 1000;

    private readonly long _startMs;
    private readonly long _intervalMs;
    private readonly long _giveUpAfterMs;
    private long _nextAttemptMs;

    public RetrySchedule(long startMs, long intervalMs, long giveUpAfterMs)
    {
        _startMs = startMs;
        _intervalMs = intervalMs;
        _giveUpAfterMs = giveUpAfterMs;
        _nextAttemptMs = startMs + intervalMs;
    }

    /// <summary>Attempts started so far.</summary>
    public int Attempts { get; private set; }

    /// <summary>True once per interval, counting the attempt.</summary>
    public bool AttemptDue(long nowMs)
    {
        if (nowMs < _nextAttemptMs) return false;
        _nextAttemptMs = nowMs + _intervalMs;
        Attempts++;
        return true;
    }

    public bool GaveUp(long nowMs) => nowMs - _startMs > _giveUpAfterMs;

    /// <summary>Whole seconds until giving up, rounded up, never below 0.</summary>
    public int SecondsLeft(long nowMs) => (int)((Math.Max(0, _startMs + _giveUpAfterMs - nowMs) + MsPerSecond - 1) / MsPerSecond);
}

/// <summary>
/// A guest whose lobby connection fails keeps trying to rejoin the same lobby, so a network blip does not end the
/// session. The game keeps running; once back in, the adapter reconnects and the join snapshot catches it up.
/// </summary>
public sealed class Rejoin
{
    public const long RetryIntervalMs = 5000;
    public const long GiveUpAfterMs = 120_000;

    private const long MsPerSecond = 1000;

    public Rejoin(ulong lobbyId)
    {
        LobbyId = lobbyId;
        Schedule = new RetrySchedule(Environment.TickCount64, RetryIntervalMs, GiveUpAfterMs);
        Log.Info($"Connection to the session lost, rejoining lobby {lobbyId} for up to {GiveUpAfterMs / MsPerSecond} s");
    }

    public ulong LobbyId { get; }

    /// <summary>The game of the lobby being rejoined, for the status while no lobby object exists.</summary>
    public string? GameId { get; init; }

    public RetrySchedule Schedule { get; }

    public bool GaveUp => Schedule.GaveUp(Environment.TickCount64);

    /// <summary>True when the next attempt is due; logs it.</summary>
    public bool AttemptDue()
    {
        if (!Schedule.AttemptDue(Environment.TickCount64)) return false;
        Log.Info($"Rejoining lobby {LobbyId} (attempt {Schedule.Attempts})");
        return true;
    }
}
