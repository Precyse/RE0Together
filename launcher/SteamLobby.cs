using System.Security.Cryptography;
using Steamworks;

namespace CoopLauncher;

/// <summary>Steam matchmaking lobby carrying the <see cref="LobbyKeys"/> data (game, protocol, builds, host options, roster).</summary>
public sealed class SteamLobby : ILobby
{
    private const string KeyStatus = "cf_status";
    private const string KeyStep = "cf_step";
    private const string KeyOptions = "cf_options";
    private const string KeyRefused = "cf_refused";
    private const char RefusedSeparator = ':';
    private const long RefusalPublishDelayMs = 1500;
    private const int JoinSecretMin = 100_000_000;  // nine digits
    private const int JoinSecretMax = 1_000_000_000;
    private const long AnswerTimeoutMs = 30_000;
    private const long LocationRetryMs = 1000;
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
    private int _createdModBuild;
    private int _createdGameBuild;
    private bool _hosting;
    private bool _locationPublished;
    private long _nextLocationMs;
    private LobbyListing? _listing;
    private bool _listingDirty = true;
    private bool _publishedPresence;
    private CSteamID _lobby;
    private List<LobbyMember> _members = new();
    private List<RefusedJoin> _refusedJoins = new();
    private LobbyFailure? _pendingFailure;
    private long _pendingFailureDueMs;

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
            var text = _lobby.IsValid() ? Text(SteamMatchmaking.GetLobbyData(_lobby, LobbyKeys.HostOptions)) : string.Empty;
            if (text == _hostOptionsText) return _hostOptions;
            _hostOptionsText = text;
            return _hostOptions = GameOptions.Decode(text);
        }
    }

    public string JoinSecret => _lobby.IsValid() ? Text(SteamMatchmaking.GetLobbyData(_lobby, LobbyKeys.JoinSecret)) : string.Empty;

    public IReadOnlyList<LobbyMember> Members => _members;

    public int Revision { get; private set; }

    public IReadOnlyList<RefusedJoin> RefusedJoins => _refusedJoins;

    /// <summary>The lobby read from its own data, again after the data or the members changed.</summary>
    public LobbyListing? Listing
    {
        get
        {
            if (!IsReady) return null;
            if (!_listingDirty && _listing != null) return _listing;
            _listingDirty = false;
            return _listing = LobbyListing.Decode(
                key => Text(SteamMatchmaking.GetLobbyData(_lobby, key)), _lobby.m_SteamID, OwnerId,
                SteamMatchmaking.GetNumLobbyMembers(_lobby), SteamMatchmaking.GetLobbyMemberLimit(_lobby), ping: null);
        }
    }

    /// <summary>The host's lobby: friends only or public as the settings say. <paramref name="gameDir"/> is where the installed mod
    /// and game builds the lobby publishes are read.</summary>
    public static SteamLobby Create(GameProfile profile, IReadOnlyDictionary<string, string> hostOptions, string? gameDir)
    {
        var lobby = new SteamLobby
        {
            GameId = profile.Id, _createdOptions = hostOptions, _hosting = true,
            _createdModBuild = ModInstaller.Status(profile, gameDir).Build, _createdGameBuild = GameBuilds.Installed(profile, gameDir),
            _createdJoinSecret = profile.RendezvousSession ? RandomNumberGenerator.GetInt32(JoinSecretMin, JoinSecretMax).ToString() : string.Empty,
        };
        lobby.Track(lobby._created, SteamMatchmaking.CreateLobby(TypeOf(AppSettings.Current.LobbyVisibility), GameOptions.Seats(profile, hostOptions)));
        return lobby;
    }

    private static ELobbyType TypeOf(LobbyVisibility visibility) =>
        visibility == LobbyVisibility.Everyone ? ELobbyType.k_ELobbyTypePublic : ELobbyType.k_ELobbyTypeFriendsOnly;

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
        if (_pendingFailure != null && Environment.TickCount64 >= _pendingFailureDueMs)
        {
            Failure = _pendingFailure;
            _pendingFailure = null;
        }
        if (_hosting && IsReady && !_locationPublished) PublishLocation();
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

    public void SetLobbyData(LobbyKey key, string value)
    {
        if (!_lobby.IsValid() || SteamMatchmaking.GetLobbyOwner(_lobby) != SteamUser.GetSteamID()) return;
        SteamMatchmaking.SetLobbyData(_lobby, LobbyDataKey(key), value);
        _listingDirty = true;
    }

    public string MemberData(LobbyMember member, MemberKey key) =>
        _lobby.IsValid() ? Text(SteamMatchmaking.GetLobbyMemberData(_lobby, new CSteamID(member.Id), MemberDataKey(key))) : string.Empty;

    private static string LobbyDataKey(LobbyKey key) => key switch
    {
        LobbyKey.Roster => LobbyKeys.Roster,
        LobbyKey.HostOptions => LobbyKeys.HostOptions,
        _ => LobbyKeys.Started,
    };

    private static string MemberDataKey(MemberKey key) => key switch
    {
        MemberKey.Step => KeyStep,
        MemberKey.Options => KeyOptions,
        _ => KeyStatus,
    };

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
        SteamMatchmaking.SetLobbyData(_lobby, LobbyKeys.Game, GameId);
        SteamMatchmaking.SetLobbyData(_lobby, LobbyKeys.Proto, Framing.ProtocolVersion.ToString());
        SteamMatchmaking.SetLobbyData(_lobby, LobbyKeys.Version, BuildCheck.LocalBuild().ToString());
        SteamMatchmaking.SetLobbyData(_lobby, LobbyKeys.HostOptions, GameOptions.Encode(_createdOptions));
        SteamMatchmaking.SetLobbyData(_lobby, LobbyKeys.App, LobbyKeys.AppMarker);
        SteamMatchmaking.SetLobbyData(_lobby, LobbyKeys.Host, Text(SteamFriends.GetPersonaName()));
        SteamMatchmaking.SetLobbyData(_lobby, LobbyKeys.Mod, _createdModBuild.ToString());
        SteamMatchmaking.SetLobbyData(_lobby, LobbyKeys.GameBuild, _createdGameBuild.ToString());
        if (_createdJoinSecret.Length > 0) SteamMatchmaking.SetLobbyData(_lobby, LobbyKeys.JoinSecret, _createdJoinSecret);
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
        GameId = Text(SteamMatchmaking.GetLobbyData(_lobby, LobbyKeys.Game));
        if (EntryRefusal(Text(SteamMatchmaking.GetLobbyData(_lobby, LobbyKeys.Proto)), Text(SteamMatchmaking.GetLobbyData(_lobby, LobbyKeys.Version))) is { } refusal)
        {
            RefuseEntry(refusal);
            return;
        }
        Announce();
        Log.Info($"Joined lobby {_lobby.m_SteamID} for game {GameId}");
    }

    /// <summary>Why this launcher cannot take part in the lobby it entered, null when it can: another launcher build than the host's
    /// (both numbered; checked first, since a newer launcher may also speak another protocol or know other games), not a co-op lobby
    /// of this protocol, or a game this launcher has no profile for.</summary>
    private LobbyFailure? EntryRefusal(string proto, string hostVersion)
    {
        var local = BuildCheck.LocalBuild();
        if (int.TryParse(hostVersion, out var host) && host != BuildInfo.DevBuild && local != BuildInfo.DevBuild && host != local)
            return new LobbyFailure(RefusalKind.LauncherBuild, $"host is on build {host}, this is build {local}", RefusalVersions.Format(host, local));
        if (string.IsNullOrEmpty(GameId) || proto != Framing.ProtocolVersion.ToString())
            return new LobbyFailure(RefusalKind.NotCoopLobby, $"lobby is not a compatible co-op lobby (game '{GameId}', proto '{proto}')");
        if (!GameProfile.ListIds().Contains(GameId))
            return new LobbyFailure(RefusalKind.UnknownGame, $"lobby is for game '{GameId}', which this launcher does not know", GameId);
        return null;
    }

    /// <summary>A launcher that cannot take part tells the host why before it leaves (<c>cf_refused=LauncherBuild:&lt;its build&gt;</c>), and
    /// leaves a moment later so Steam has carried the line; any other refusal is final at once.</summary>
    private void RefuseEntry(LobbyFailure failure)
    {
        if (failure.Kind != RefusalKind.LauncherBuild)
        {
            Failure = failure;
            return;
        }
        SteamMatchmaking.SetLobbyMemberData(_lobby, KeyRefused, $"{failure.Kind}{RefusedSeparator}{BuildCheck.LocalBuild()}");
        _pendingFailure = failure;
        _pendingFailureDueMs = Environment.TickCount64 + RefusalPublishDelayMs;
    }

    /// <summary>The refusal a launcher published, null for no line or one this launcher does not understand.</summary>
    internal static (RefusalKind Kind, string Detail)? ParseRefused(string text)
    {
        var parts = text.Split(RefusedSeparator, 2);
        return parts.Length == 2 && Enum.TryParse<RefusalKind>(parts[0], out var kind) && Enum.IsDefined(kind) && !char.IsDigit(parts[0][0]) ? (kind, parts[1]) : null;
    }

    /// <summary>Publishes our own game/proto so other members can filter us, then marks the lobby usable.</summary>
    private void Announce()
    {
        SteamMatchmaking.SetLobbyMemberData(_lobby, LobbyKeys.Game, GameId);
        SteamMatchmaking.SetLobbyMemberData(_lobby, LobbyKeys.Proto, Framing.ProtocolVersion.ToString());
        IsReady = true;
        Refresh();
    }

    /// <summary>Publishes where this host is on the network (<see cref="LobbyKeys.Location"/>) once Steam knows it, so searchers
    /// can estimate their ping to it; tried every <see cref="LocationRetryMs"/> until it works.</summary>
    private void PublishLocation()
    {
        var now = Environment.TickCount64;
        if (now < _nextLocationMs) return;
        _nextLocationMs = now + LocationRetryMs;
        if (SteamNetworkingUtils.GetLocalPingLocation(out var location) < 0) return;
        SteamNetworkingUtils.ConvertPingLocationToString(ref location, out var text, Constants.k_cchMaxSteamNetworkingPingLocationString);
        if (string.IsNullOrEmpty(text)) return;
        SteamMatchmaking.SetLobbyData(_lobby, LobbyKeys.Location, text);
        _locationPublished = true;
        _listingDirty = true;
    }

    private void Refresh()
    {
        if (!IsReady) return;
        _listingDirty = true;
        var self = SteamUser.GetSteamID();
        var members = new List<LobbyMember>();
        var refused = new List<RefusedJoin>();
        var count = SteamMatchmaking.GetNumLobbyMembers(_lobby);
        for (var i = 0; i < count; i++)
        {
            var id = SteamMatchmaking.GetLobbyMemberByIndex(_lobby, i);
            var name = Text(SteamFriends.GetFriendPersonaName(id));
            if (id != self && ParseRefused(Text(SteamMatchmaking.GetLobbyMemberData(_lobby, id, KeyRefused))) is var (kind, detail)) refused.Add(new RefusedJoin(id.m_SteamID, name, kind, detail));
            if (id != self && !IsCompatible(id)) continue;
            members.Add(new LobbyMember(id.m_SteamID, name));
        }
        _members = members;
        _refusedJoins = refused;
        Revision++;
        // Steam lists no members of a lobby we are no longer in: dropped, kicked or timed out.
        if (members.All(m => m.Id != self.m_SteamID))
            Failure ??= new LobbyFailure(RefusalKind.SteamOffline, "no longer in the lobby (connection lost)");
    }

    private bool IsCompatible(CSteamID member) =>
        Text(SteamMatchmaking.GetLobbyMemberData(_lobby, member, LobbyKeys.Game)) == GameId &&
        Text(SteamMatchmaking.GetLobbyMemberData(_lobby, member, LobbyKeys.Proto)) == Framing.ProtocolVersion.ToString();

    /// <summary>Steamworks.NET returns null when Steam hands back no string (a member that just left, data not yet
    /// delivered, a persona not yet loaded). Everything above this class reads such a value as empty.</summary>
    private static string Text(string? steamString) => steamString ?? string.Empty;
}
