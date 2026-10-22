namespace CoopLauncher;

/// <summary>Two-member stand-in for a Steam lobby: the peer is a member while its datagrams arrive.</summary>
public sealed class LocalLobby : ILobby
{
    private const int Seats = 2;

    private readonly LocalTransport _transport;
    private readonly bool _isHost;
    private readonly Dictionary<(ulong Member, MemberKey Key), string> _data = new();
    private readonly Dictionary<LobbyKey, string> _lobbyData = new();
    private bool _peerPresent;

    public LocalLobby(LocalTransport transport, string gameId, bool isHost, IReadOnlyDictionary<string, string>? hostOptions = null)
    {
        HostOptions = hostOptions ?? new Dictionary<string, string>();
        _transport = transport;
        _isHost = isHost;
        GameId = gameId;
    }

    public bool IsReady => true;

    public LobbyFailure? Failure => null;

    public string? GameId { get; }

    public ulong Id => 0;

    public IReadOnlyDictionary<string, string> HostOptions { get; private set; }

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

    public void SetLobbyData(LobbyKey key, string value)
    {
        if (!_isHost) return;
        if (key == LobbyKey.HostOptions) HostOptions = GameOptions.Decode(value);
        else _lobbyData[key] = value;
    }

    public LobbyListing Listing => LobbyListing.Decode(LobbyText, Id, OwnerId, Members.Count, Seats, ping: null);

    public void SetMemberData(MemberKey key, string value) => _data[(_transport.LocalId, key)] = value;

    public string MemberData(LobbyMember member, MemberKey key) => _data.GetValueOrDefault((member.Id, key), string.Empty);

    public void Dispose()
    {
    }

    private string LobbyText(string key) => key switch
    {
        LobbyKeys.Game => GameId ?? string.Empty,
        LobbyKeys.HostOptions => GameOptions.Encode(HostOptions),
        LobbyKeys.Roster => _lobbyData.GetValueOrDefault(LobbyKey.Roster, string.Empty),
        LobbyKeys.Started => _lobbyData.GetValueOrDefault(LobbyKey.Started, string.Empty),
        _ => string.Empty,
    };

    private static LobbyMember Member(ulong port) => new(port, $"local-{port}");
}
