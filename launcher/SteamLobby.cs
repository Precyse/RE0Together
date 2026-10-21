using System.Security.Cryptography;
using Steamworks;

namespace CoopLauncher;

/// <summary>Steam matchmaking lobby carrying cf_game / cf_proto / cf_ver.</summary>
public sealed class SteamLobby : ILobby
{
    private const string KeyGame = "cf_game";
    private const string KeyProto = "cf_proto";
    private const string KeyVersion = "cf_ver";
    private const string KeyHostOptions = "cf_opts";
    private const string KeyJoinSecret = "cf_join_pw";
    private const string KeyStatus = "cf_status";
    private const string KeyStep = "cf_step";
    private const int JoinSecretMin = 100_000_000;  // nine digits
    private const int JoinSecretMax = 1_000_000_000;
    private const long AnswerTimeoutMs = 30_000;
    private const uint EnterSuccess = (uint)EChatRoomEnterResponse.k_EChatRoomEnterResponseSuccess;

    private readonly CallResult<LobbyCreated_t> _created;
    private readonly CallResult<LobbyEnter_t> _entered;
    private readonly Callback<LobbyChatUpdate_t> _chatUpdate;
    private readonly Callback<LobbyDataUpdate_t> _dataUpdate;
    private readonly long _answerDeadlineMs = Environment.TickCount64 + AnswerTimeoutMs;
    private IReadOnlyDictionary<string, string> _createdOptions = new Dictionary<string, string>();
    private IReadOnlyDictionary<string, string> _hostOptions = new Dictionary<string, string>();
    private string _hostOptionsText = string.Empty;
    private string _createdJoinSecret = string.Empty;
    private bool _publishedPresence;
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

    public LobbyFailure? Failure { get; private set; }

    public string? GameId { get; private set; }

    public ulong Id => _lobby.m_SteamID;

    public ulong OwnerId => IsReady ? SteamMatchmaking.GetLobbyOwner(_lobby).m_SteamID : 0;

    /// <summary>Decoded again only when the lobby's text changes, so callers can tell a change by reference.</summary>
    public IReadOnlyDictionary<string, string> HostOptions
    {
        get
        {
            var text = _lobby.IsValid() ? Text(SteamMatchmaking.GetLobbyData(_lobby, KeyHostOptions)) : string.Empty;
            if (text == _hostOptionsText) return _hostOptions;
            _hostOptionsText = text;
            return _hostOptions = GameOptions.Decode(text);
        }
    }

    public string JoinSecret => _lobby.IsValid() ? Text(SteamMatchmaking.GetLobbyData(_lobby, KeyJoinSecret)) : string.Empty;

    public IReadOnlyList<LobbyMember> Members => _members;

    public int Revision { get; private set; }

    public static SteamLobby Create(GameProfile profile, IReadOnlyDictionary<string, string> hostOptions)
    {
        var lobby = new SteamLobby
        {
            GameId = profile.Id, _createdOptions = hostOptions,
            _createdJoinSecret = profile.RendezvousSession ? RandomNumberGenerator.GetInt32(JoinSecretMin, JoinSecretMax).ToString() : string.Empty,
        };
        lobby.Track(lobby._created, SteamMatchmaking.CreateLobby(ELobbyType.k_ELobbyTypeFriendsOnly, GameOptions.Seats(profile, hostOptions)));
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
            Failure = new LobbyFailure(RefusalKind.SteamOffline, "Steam did not answer the lobby request in time");
    }

    public void Dispose()
    {
        if (_publishedPresence) LobbyPresence.Clear();
        if (_lobby.IsValid()) SteamMatchmaking.LeaveLobby(_lobby);
        _created.Dispose();
        _entered.Dispose();
        _chatUpdate.Dispose();
        _dataUpdate.Dispose();
    }

    public void SetMemberData(MemberKey key, string value)
    {
        if (_lobby.IsValid()) SteamMatchmaking.SetLobbyMemberData(_lobby, MemberDataKey(key), value);
    }

    public string MemberData(LobbyMember member, MemberKey key) =>
        _lobby.IsValid() ? Text(SteamMatchmaking.GetLobbyMemberData(_lobby, new CSteamID(member.Id), MemberDataKey(key))) : string.Empty;

    private static string MemberDataKey(MemberKey key) => key == MemberKey.Step ? KeyStep : KeyStatus;

    private void Track<T>(CallResult<T> result, SteamAPICall_t call) where T : struct
    {
        if (call == SteamAPICall_t.Invalid)
            Failure = new LobbyFailure(RefusalKind.SteamOffline, "Steam did not accept the lobby request (is Steam online?)");
        else result.Set(call);
    }

    private void OnCreated(LobbyCreated_t result, bool ioFailure)
    {
        if (ioFailure)
        {
            Failure = new LobbyFailure(RefusalKind.SteamOffline, "lobby creation failed (Steam did not answer)");
            return;
        }
        if (result.m_eResult != EResult.k_EResultOK)
        {
            Failure = new LobbyFailure(Refusals.FromCreateResult((int)result.m_eResult), $"lobby creation failed ({result.m_eResult})");
            return;
        }
        _lobby = new CSteamID(result.m_ulSteamIDLobby);
        SteamMatchmaking.SetLobbyData(_lobby, KeyGame, GameId);
        SteamMatchmaking.SetLobbyData(_lobby, KeyProto, Framing.ProtocolVersion.ToString());
        SteamMatchmaking.SetLobbyData(_lobby, KeyVersion, BuildCheck.LocalBuild().ToString());
        SteamMatchmaking.SetLobbyData(_lobby, KeyHostOptions, GameOptions.Encode(_createdOptions));
        if (_createdJoinSecret.Length > 0) SteamMatchmaking.SetLobbyData(_lobby, KeyJoinSecret, _createdJoinSecret);
        Announce();
        LobbyPresence.Publish(_lobby.m_SteamID, GameId!);
        _publishedPresence = true;
        Log.Event("Lobby created");
        Log.Info($"Lobby created: {_lobby.m_SteamID}");
    }

    private void OnEntered(LobbyEnter_t result, bool ioFailure)
    {
        if (ioFailure)
        {
            Failure = new LobbyFailure(RefusalKind.SteamOffline, "lobby join failed (Steam did not answer)");
            return;
        }
        if (result.m_EChatRoomEnterResponse != EnterSuccess)
        {
            Failure = new LobbyFailure(
                Refusals.FromEnterResponse(result.m_EChatRoomEnterResponse), $"lobby join failed (response {result.m_EChatRoomEnterResponse})");
            return;
        }
        _lobby = new CSteamID(result.m_ulSteamIDLobby);
        GameId = Text(SteamMatchmaking.GetLobbyData(_lobby, KeyGame));
        Failure = EntryRefusal(Text(SteamMatchmaking.GetLobbyData(_lobby, KeyProto)), Text(SteamMatchmaking.GetLobbyData(_lobby, KeyVersion)));
        if (Failure != null) return;
        Announce();
        Log.Info($"Joined lobby {_lobby.m_SteamID} for game {GameId}");
    }

    /// <summary>Why this launcher cannot take part in the lobby it entered, null when it can: not a co-op lobby of this
    /// protocol, a game this launcher has no profile for, or another launcher build than the host's (both numbered).</summary>
    private LobbyFailure? EntryRefusal(string proto, string hostVersion)
    {
        if (string.IsNullOrEmpty(GameId) || proto != Framing.ProtocolVersion.ToString())
            return new LobbyFailure(RefusalKind.NotCoopLobby, $"lobby is not a compatible co-op lobby (game '{GameId}', proto '{proto}')");
        if (!GameProfile.ListIds().Contains(GameId))
            return new LobbyFailure(RefusalKind.UnknownGame, $"lobby is for game '{GameId}', which this launcher does not know", GameId);
        var local = BuildCheck.LocalBuild();
        if (int.TryParse(hostVersion, out var host) && host != BuildInfo.DevBuild && local != BuildInfo.DevBuild && host != local)
            return new LobbyFailure(RefusalKind.LauncherBuild, $"host is on build {host}, this is build {local}", host.ToString());
        return null;
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
            members.Add(new LobbyMember(id.m_SteamID, Text(SteamFriends.GetFriendPersonaName(id))));
        }
        _members = members;
        Revision++;
        // Steam lists no members of a lobby we are no longer in: dropped, kicked or timed out.
        if (members.All(m => m.Id != self.m_SteamID))
            Failure ??= new LobbyFailure(RefusalKind.SteamOffline, "no longer in the lobby (connection lost)");
    }

    private bool IsCompatible(CSteamID member) =>
        Text(SteamMatchmaking.GetLobbyMemberData(_lobby, member, KeyGame)) == GameId &&
        Text(SteamMatchmaking.GetLobbyMemberData(_lobby, member, KeyProto)) == Framing.ProtocolVersion.ToString();

    /// <summary>Steamworks.NET returns null when Steam hands back no string (a member that just left, data not yet
    /// delivered, a persona not yet loaded). Everything above this class reads such a value as empty.</summary>
    private static string Text(string? steamString) => steamString ?? string.Empty;
}
