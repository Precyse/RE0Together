namespace CoopLauncher;

/// <summary>Two-member stand-in for a Steam lobby: the peer is a member while its datagrams arrive.</summary>
public sealed class LocalLobby : ILobby
{
    private readonly LocalTransport _transport;
    private readonly bool _isHost;
    private bool _peerPresent;

    public LocalLobby(LocalTransport transport, string gameId, bool isHost)
    {
        _transport = transport;
        _isHost = isHost;
        GameId = gameId;
    }

    public bool IsReady => true;

    public string? Failure => null;

    public string? GameId { get; }

    public ulong Id => 0;

    public ulong OwnerId => _isHost ? _transport.LocalId : _transport.PeerId;

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

    public void Dispose()
    {
    }

    private static LobbyMember Member(ulong port) => new(port, $"local-{port}");
}
