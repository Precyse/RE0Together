namespace CoopLauncher;

/// <summary>
/// A guest whose lobby connection fails keeps trying to rejoin the same lobby, so a network blip does not end the
/// session. The game keeps running; once back in, the adapter reconnects and the join snapshot catches it up.
/// </summary>
public sealed class Rejoin
{
    private const long RetryIntervalMs = 5000;
    private const long GiveUpAfterMs = 120_000;

    private readonly long _deadlineMs;
    private long _nextAttemptMs;
    private int _attempts;

    public Rejoin(ulong lobbyId)
    {
        LobbyId = lobbyId;
        var now = Environment.TickCount64;
        _deadlineMs = now + GiveUpAfterMs;
        _nextAttemptMs = now + RetryIntervalMs;
        Log.Info($"Connection to the session lost, rejoining lobby {lobbyId} for up to {GiveUpAfterMs / 1000} s");
    }

    public ulong LobbyId { get; }

    public bool GaveUp => Environment.TickCount64 > _deadlineMs;

    /// <summary>True when the next attempt is due; logs it.</summary>
    public bool AttemptDue()
    {
        var now = Environment.TickCount64;
        if (now < _nextAttemptMs) return false;
        _nextAttemptMs = now + RetryIntervalMs;
        Log.Info($"Rejoining lobby {LobbyId} (attempt {++_attempts})");
        return true;
    }
}
