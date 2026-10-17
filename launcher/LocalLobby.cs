namespace CoopLauncher;

/// <summary>Two-member stand-in for a Steam lobby: the peer is a member while its datagrams arrive.</summary>
public sealed class LocalLobby : ILobby
{
    private readonly LocalTransport _transport;
    private readonly bool _isHost;
    private readonly Dictionary<ulong, string> _status = new();
    private bool _peerPresent;

    public LocalLobby(LocalTransport transport, string gameId, bool isHost, IReadOnlyDictionary<string, string>? hostOptions = null)
    {
        HostOptions = hostOptions ?? new Dictionary<string, string>();
        _transport = transport;
        _isHost = isHost;
        GameId = gameId;
    }

    public bool IsReady => true;

    public string? Failure => null;

    public string? GameId { get; }

    public ulong Id => 0;

    public IReadOnlyDictionary<string, string> HostOptions { get; }

    public ulong OwnerId => _isHost ? _transport.LocalId : _transport.PeerId;

    public string JoinSecret => string.Empty;

    public int Revision { get; private set; }

    public IReadOnlyList<LobbyMember> Members
    {
        get
        {
            var members = new List<LobbyMember> { Member(_transport.LocalId) };
            if (_peerPresent) members.Add(Member(_transport.PeerId));
            return members;
        }
    }

    public void Pump()
    {
        if (_transport.PeerPresent == _peerPresent) return;
        _peerPresent = _transport.PeerPresent;
        Revision++;
    }

    public void SetMemberStatus(string status) => _status[_transport.LocalId] = status;

    public string MemberStatus(LobbyMember member) => _status.GetValueOrDefault(member.Id, string.Empty);

    public void Dispose()
    {
    }

    private static LobbyMember Member(ulong port) => new(port, $"local-{port}");
}
