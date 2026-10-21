using Steamworks;

namespace CoopLauncher;

/// <summary>
/// Finds other people's lobbies: a lobby list search for every co-op launcher lobby (the <see cref="LobbyKeys.App"/> marker and
/// this protocol, worldwide), and the data of the lobbies friends host. Each is read into a <see cref="LobbyListing"/> with the
/// ping estimated from the host's <see cref="LobbyKeys.Location"/>. Steam, loop thread only.
/// </summary>
public sealed class LobbyDirectory : IDisposable
{
    public const int MaxResults = 50;
    private const long AnswerTimeoutMs = 30_000;

    private readonly CallResult<LobbyMatchList_t> _listed;
    private readonly Callback<LobbyDataUpdate_t> _dataUpdate;
    private readonly Dictionary<ulong, LobbyListing> _friendListings = new();
    private readonly HashSet<ulong> _friendLobbies = new();
    private long _answerDeadlineMs;

    public LobbyDirectory()
    {
        _listed = CallResult<LobbyMatchList_t>.Create(OnListed);
        _dataUpdate = Callback<LobbyDataUpdate_t>.Create(OnDataUpdate);
    }

    /// <summary>The last search's lobbies, unfiltered (see <see cref="Select"/>).</summary>
    public IReadOnlyList<LobbyListing> Results { get; private set; } = [];

    /// <summary>A search was asked for and Steam has not answered yet.</summary>
    public bool Searching { get; private set; }

    /// <summary>Increments whenever <see cref="Results"/> or a friend's listing changed.</summary>
    public int Revision { get; private set; }

    /// <summary>The data of a friend's lobby once Steam delivered it (see <see cref="ReadFriends"/>), else null.</summary>
    public LobbyListing? FriendListing(ulong lobbyId) => _friendListings.GetValueOrDefault(lobbyId);

    /// <summary>Asks Steam for the co-op lobbies; the answer lands in <see cref="Results"/>. A search under way is left to finish
    /// unless Steam has not answered for <see cref="AnswerTimeoutMs"/>.</summary>
    public void Search()
    {
        if (Searching && Environment.TickCount64 < _answerDeadlineMs) return;
        SteamMatchmaking.AddRequestLobbyListStringFilter(LobbyKeys.App, LobbyKeys.AppMarker, ELobbyComparison.k_ELobbyComparisonEqual);
        SteamMatchmaking.AddRequestLobbyListStringFilter(LobbyKeys.Proto, Framing.ProtocolVersion.ToString(), ELobbyComparison.k_ELobbyComparisonEqual);
        SteamMatchmaking.AddRequestLobbyListDistanceFilter(ELobbyDistanceFilter.k_ELobbyDistanceFilterWorldwide);
        SteamMatchmaking.AddRequestLobbyListResultCountFilter(MaxResults);
        var call = SteamMatchmaking.RequestLobbyList();
        Searching = call != SteamAPICall_t.Invalid;
        if (!Searching) return;
        _answerDeadlineMs = Environment.TickCount64 + AnswerTimeoutMs;
        _listed.Set(call);
    }

    /// <summary>Asks Steam for the data of each friend's lobby; <see cref="FriendListing"/> answers once it arrives.</summary>
    public void ReadFriends(IEnumerable<FriendLobby> friends)
    {
        _friendLobbies.Clear();
        foreach (var friend in friends)
        {
            _friendLobbies.Add(friend.LobbyId);
            SteamMatchmaking.RequestLobbyData(new CSteamID(friend.LobbyId));
        }
        foreach (var gone in _friendListings.Keys.Except(_friendLobbies).ToList()) _friendListings.Remove(gone);
    }

    /// <summary>The lobbies to offer: of a game this launcher has a profile for, not hosted by <paramref name="ownId"/>, once per
    /// lobby, nearest first (ping ascending, unknown last). Pure.</summary>
    public static IReadOnlyList<LobbyListing> Select(IEnumerable<LobbyListing> listings, IReadOnlyCollection<string> knownGames, ulong ownId) =>
        listings.Where(listing => listing.HostId != ownId && knownGames.Contains(listing.GameId))
            .GroupBy(listing => listing.LobbyId).Select(group => group.First())
            .OrderBy(listing => listing.PingMs ?? int.MaxValue)
            .ToList();

    public void Dispose()
    {
        _listed.Dispose();
        _dataUpdate.Dispose();
    }

    private void OnListed(LobbyMatchList_t result, bool ioFailure)
    {
        Searching = false;
        if (ioFailure) return;
        var found = new List<LobbyListing>();
        for (var i = 0; i < result.m_nLobbiesMatching; i++) found.Add(Read(SteamMatchmaking.GetLobbyByIndex(i)));
        Results = found;
        Revision++;
    }

    private void OnDataUpdate(LobbyDataUpdate_t update)
    {
        var lobbyId = update.m_ulSteamIDLobby;
        if (update.m_bSuccess == 0 || update.m_ulSteamIDMember != lobbyId || !_friendLobbies.Contains(lobbyId)) return;
        _friendListings[lobbyId] = Read(new CSteamID(lobbyId));
        Revision++;
    }

    private static LobbyListing Read(CSteamID lobby)
    {
        string Data(string key) => SteamMatchmaking.GetLobbyData(lobby, key) ?? string.Empty;
        return LobbyListing.Decode(
            Data, lobby.m_SteamID, SteamMatchmaking.GetLobbyOwner(lobby).m_SteamID, SteamMatchmaking.GetNumLobbyMembers(lobby),
            SteamMatchmaking.GetLobbyMemberLimit(lobby), PingMs(Data(LobbyKeys.Location)));
    }

    /// <summary>Steam's estimate of the round trip to a host from its published location; null when there is none or Steam cannot say yet.</summary>
    private static int? PingMs(string location)
    {
        if (location.Length == 0 || !SteamNetworkingUtils.ParsePingLocationString(location, out var parsed)) return null;
        var ping = SteamNetworkingUtils.EstimatePingTimeFromLocalHost(ref parsed);
        return ping < 0 ? null : ping;
    }
}
