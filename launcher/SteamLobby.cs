using Steamworks;

namespace CoopLauncher;

/// <summary>Steam matchmaking lobby carrying cf_game / cf_proto / cf_ver.</summary>
public sealed class SteamLobby : ILobby
{
    private const string KeyGame = "cf_game";
    private const string KeyProto = "cf_proto";
    private const string KeyVersion = "cf_ver";
    private const string LauncherVersion = "1.0.0";
    private const long AnswerTimeoutMs = 30_000;
    private const uint EnterSuccess = (uint)EChatRoomEnterResponse.k_EChatRoomEnterResponseSuccess;

    private readonly CallResult<LobbyCreated_t> _created;
    private readonly CallResult<LobbyEnter_t> _entered;
    private readonly Callback<LobbyChatUpdate_t> _chatUpdate;
    private readonly Callback<LobbyDataUpdate_t> _dataUpdate;
    private readonly long _answerDeadlineMs = Environment.TickCount64 + AnswerTimeoutMs;
    private CSteamID _lobby;
    private List<LobbyMember> _members = new();

    private SteamLobby()
    {
        _created = CallResult<LobbyCreated_t>.Create(OnCreated);
        _entered = CallResult<LobbyEnter_t>.Create(OnEntered);
        _chatUpdate = Callback<LobbyChatUpdate_t>.Create(_ => Refresh());
        _dataUpdate = Callback<LobbyDataUpdate_t>.Create(_ => Refresh());
    }

    public bool IsReady { get; private set; }

    public string? Failure { get; private set; }

    public string? GameId { get; private set; }

    public ulong Id => _lobby.m_SteamID;

    public ulong OwnerId => IsReady ? SteamMatchmaking.GetLobbyOwner(_lobby).m_SteamID : 0;

    public IReadOnlyList<LobbyMember> Members => _members;

    public int Revision { get; private set; }

    public static SteamLobby Create(GameProfile profile)
    {
        var lobby = new SteamLobby { GameId = profile.Id };
        lobby.Track(lobby._created, SteamMatchmaking.CreateLobby(ELobbyType.k_ELobbyTypeFriendsOnly, profile.MaxPlayers));
        return lobby;
    }

    public static SteamLobby Join(ulong lobbyId)
    {
        Log.Info($"Joining lobby {lobbyId}");
        var lobby = new SteamLobby();
        lobby.Track(lobby._entered, SteamMatchmaking.JoinLobby(new CSteamID(lobbyId)));
        return lobby;
    }

    /// <summary>A request Steam never answers must end as a failure the window shows, not as silence.</summary>
    public void Pump()
    {
        if (!IsReady && Failure == null && Environment.TickCount64 > _answerDeadlineMs)
            Failure = "Steam did not answer the lobby request in time";
    }

    public void Dispose()
    {
        if (_lobby.IsValid()) SteamMatchmaking.LeaveLobby(_lobby);
        _created.Dispose();
        _entered.Dispose();
        _chatUpdate.Dispose();
        _dataUpdate.Dispose();
    }

    private void Track<T>(CallResult<T> result, SteamAPICall_t call) where T : struct
    {
        if (call == SteamAPICall_t.Invalid) Failure = "Steam did not accept the lobby request (is Steam online?)";
        else result.Set(call);
    }

    private void OnCreated(LobbyCreated_t result, bool ioFailure)
    {
        if (ioFailure || result.m_eResult != EResult.k_EResultOK)
        {
            Failure = $"lobby creation failed ({result.m_eResult})";
            return;
        }
        _lobby = new CSteamID(result.m_ulSteamIDLobby);
        SteamMatchmaking.SetLobbyData(_lobby, KeyGame, GameId);
        SteamMatchmaking.SetLobbyData(_lobby, KeyProto, Framing.ProtocolVersion.ToString());
        SteamMatchmaking.SetLobbyData(_lobby, KeyVersion, LauncherVersion);
        Announce();
        Log.Info($"Lobby created: {_lobby.m_SteamID}");
    }

    private void OnEntered(LobbyEnter_t result, bool ioFailure)
    {
        if (ioFailure || result.m_EChatRoomEnterResponse != EnterSuccess)
        {
            Failure = $"lobby join failed (response {result.m_EChatRoomEnterResponse})";
            return;
        }
        _lobby = new CSteamID(result.m_ulSteamIDLobby);
        GameId = SteamMatchmaking.GetLobbyData(_lobby, KeyGame);
        var proto = SteamMatchmaking.GetLobbyData(_lobby, KeyProto);
        if (string.IsNullOrEmpty(GameId) || proto != Framing.ProtocolVersion.ToString())
        {
            Failure = $"lobby is not a compatible co-op lobby (game '{GameId}', proto '{proto}')";
            return;
        }
        Announce();
        Log.Info($"Joined lobby {_lobby.m_SteamID} for game {GameId}");
    }

    /// <summary>Publishes our own game/proto so other members can filter us, then marks the lobby usable.</summary>
    private void Announce()
    {
        SteamMatchmaking.SetLobbyMemberData(_lobby, KeyGame, GameId);
        SteamMatchmaking.SetLobbyMemberData(_lobby, KeyProto, Framing.ProtocolVersion.ToString());
        IsReady = true;
        Refresh();
    }

    private void Refresh()
    {
        if (!IsReady) return;
        var self = SteamUser.GetSteamID();
        var members = new List<LobbyMember>();
        var count = SteamMatchmaking.GetNumLobbyMembers(_lobby);
        for (var i = 0; i < count; i++)
        {
            var id = SteamMatchmaking.GetLobbyMemberByIndex(_lobby, i);
            if (id != self && !IsCompatible(id)) continue;
            members.Add(new LobbyMember(id.m_SteamID, SteamFriends.GetFriendPersonaName(id)));
        }
        _members = members;
        Revision++;
        // Steam lists no members of a lobby we are no longer in: dropped, kicked or timed out.
        if (members.All(m => m.Id != self.m_SteamID)) Failure ??= "no longer in the lobby (connection lost)";
    }

    private bool IsCompatible(CSteamID member) =>
        SteamMatchmaking.GetLobbyMemberData(_lobby, member, KeyGame) == GameId &&
        SteamMatchmaking.GetLobbyMemberData(_lobby, member, KeyProto) == Framing.ProtocolVersion.ToString();
}
