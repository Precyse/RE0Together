namespace CoopLauncher;

/// <summary>
/// A guest keeps an eye on its host's published lobby. When the host crashes or relaunches it opens a new lobby with a
/// new code; this notices it, so the guest can follow instead of staying behind in an emptied lobby.
/// </summary>
public sealed class HostFollow
{
    private const long PollIntervalMs = 2000;

    private readonly ulong _hostId;
    private readonly string _gameId;
    private long _nextPollMs;

    public HostFollow(ulong hostId, string gameId)
    {
        _hostId = hostId;
        _gameId = gameId;
    }

    /// <summary>The host's new lobby for the same game, or null while the host is still a member of <paramref name="current"/>
    /// (a presence value read just after joining can be stale) or publishes none.</summary>
    public ulong? MovedTo(ILobby current)
    {
        if (current.Members.Any(member => member.Id == _hostId)) return null;
        var now = Environment.TickCount64;
        if (now < _nextPollMs) return null;
        _nextPollMs = now + PollIntervalMs;
        var (lobby, game) = LobbyPresence.Read(_hostId);
        return lobby != 0 && lobby != current.Id && game == _gameId ? lobby : null;
    }
}
