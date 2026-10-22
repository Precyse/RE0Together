using System.Collections.Concurrent;
using System.Net.Sockets;
using CoopLauncher.Gui;

namespace CoopLauncher;

/// <summary>
/// Wires transport, lobby, bridge and session together and runs the ~100 Hz main loop. The CLI runs one session and exits;
/// an interactive app (GUI) stays up, takes Host/Join/Leave commands and returns to idle when a session ends. The status
/// it publishes is built in App.Status.cs, the reconnecting paths (lobby lost, host left) are in App.Reconnect.cs.
/// </summary>
public sealed partial class App
{
    private const int PumpIntervalMs = 10;
    private const long FriendPollMs = 5000;
    private const long LobbySearchMs = 10_000;
    private const long PendingResetCheckMs = 5000;

    private readonly CliOptions _options;
    private readonly bool _interactive;
    private readonly ConcurrentQueue<Action> _commands = new();
    private readonly HashSet<string> _pendingResets = new();
    private volatile bool _stopRequested;
    private SteamBootstrap? _steam;
    private SteamAvatars? _avatars;
    private LobbyDirectory? _directory;
    private ITransport? _transport;
    private ILobby? _lobby;
    private LoopbackBridge? _bridge;
    private Session? _session;
    private SaveSyncCoordinator? _saveSync;
    private LogForwarder? _logForwarder;
    private CrashSender? _crashSender;
    private NativeJoin? _nativeJoin;
    private bool _sessionStarted;
    private GameProfile? _profile;
    private string? _gameDir;
    private ulong _hostId;
    private bool _launchPending;
    private bool _startHeld;
    private bool _startRequested;
    private bool _hostStarted;
    private long? _launchedAtMs;
    private bool _bridgeWasReady;
    private SaveProgressMeter _saveMeter = new();
    private BuildCheck? _buildCheck;
    private HostFollow? _follow;
    private ulong _joining;
    private string? _opening;
    private Refusal? _refusal;
    private PendingInvite? _invite;
    private long _nextResetCheckMs;
    private bool _watchLobbies;
    private long _nextLobbySearchMs;
    private int _directoryRevision = -1;

    public App(CliOptions options, bool interactive = false)
    {
        _options = options;
        _interactive = interactive;
    }

    public AppStatus Status { get; private set; } = AppStatus.Idle;

    /// <summary>Raised on the main loop thread when Status changes.</summary>
    public event Action<AppStatus>? StatusChanged;

    /// <summary>Raised on the main loop thread when Steam hands over an avatar (Steam transport only).</summary>
    public event Action<AvatarImage>? AvatarLoaded;

    /// <summary>Raised on the main loop thread when the player accepted an invite (the window comes forward).</summary>
    public event Action? InviteAccepted;

    public void Stop() => _stopRequested = true;

    /// <summary>The window's loop host could not run the loop (Steam not running or gone): shown until the loop runs again.</summary>
    public void MarkOffline(OfflineReason reason = OfflineReason.None)
    {
        if (Status.State == AppState.Offline && Status.Offline == reason) return;
        Status = new AppStatus(AppState.Offline) { Offline = reason };
        StatusChanged?.Invoke(Status);
    }

    public void Host(string gameId) => _commands.Enqueue(() => Open(Command.Host, gameId));

    public void Join(ulong lobbyId) => _commands.Enqueue(() => Open(Command.Join, lobbyId.ToString()));

    public void Leave() => _commands.Enqueue(() =>
    {
        CancelReconnect();
        EndSession();
    });

    /// <summary>Leaves the current lobby and joins the one the pending invite names.</summary>
    public void SwitchToInvite() => _commands.Enqueue(() =>
    {
        if (_invite is not { } invite) return;
        CancelReconnect();
        EndSession();
        Open(Command.Join, invite.LobbyId.ToString());
    });

    /// <summary>The host's START GAME: opens the start gate, so the held launch goes ahead. Ignored for a guest and when nothing is held.</summary>
    public void StartGame() => _commands.Enqueue(() =>
    {
        if (_startHeld && _hostId == _transport!.LocalId) _startRequested = true;
    });

    /// <summary>The host plays on without the partners it is holding for a reconnect.</summary>
    public void PlaySolo() => _commands.Enqueue(() =>
    {
        if (_session is not { IsHost: true } session) return;
        session.ReleaseHeld();
        Log.Event("Playing solo");
    });

    /// <summary>The window shows the Lobbies tab (or stopped): while it does and the app is idle the lobby list is searched now
    /// and every <see cref="LobbySearchMs"/>.</summary>
    public void WatchLobbies(bool on) => _commands.Enqueue(() =>
    {
        _watchLobbies = on;
        _nextLobbySearchMs = 0;
    });

    public void ClearRefusal() => _commands.Enqueue(() => _refusal = null);

    /// <summary>Raises <see cref="AvatarLoaded"/> again for every avatar already read (a window that subscribed late).</summary>
    public void ResendAvatars() => _commands.Enqueue(() => _avatars?.ResendAll());

    public int Run()
    {
        _steam = _options.Transport == TransportKind.Steam ? new SteamBootstrap() : null;
        using var steam = _steam;
        using var directory = _directory = _steam != null ? new LobbyDirectory() : null;
        _transport = _steam != null
            ? new SteamTransport(id => _lobby?.Members.Any(m => m.Id == id) == true || _session?.HasPeer(id) == true)
            : new LocalTransport(_options.LocalPort, _options.PeerPort);
        using var transport = _transport;
        ResetAdapterSettings();
        try
        {
            StartAvatars();
            _lobby = OpenLobby(_options.Command, _options.Argument);
            if (_lobby == null && _refusal != null && !_interactive) return 1;
            return Loop();
        }
        finally
        {
            CancelReconnect();
            EndSession();
            _avatars?.Dispose();
            _avatars = null;
        }
    }

    private void StartAvatars()
    {
        if (_steam == null) return;
        _avatars = new SteamAvatars();
        _avatars.Loaded += image => AvatarLoaded?.Invoke(image);
    }

    private ILobby? OpenLobby(Command command, string? argument)
    {
        switch (command)
        {
            case Command.Host:
                return OpenHostLobby(argument!);
            case Command.Join:
                return _options.Transport == TransportKind.Steam
                    ? JoinLobby(ulong.Parse(argument!))
                    : new LocalLobby((LocalTransport)_transport!, _options.Game ?? SoleProfileId(), isHost: false, GameOptions.Decode(_options.HostOptions));
            default:
                Log.Info("Waiting for a Steam invite");
                return null;
        }
    }

    /// <summary>Opens the host's lobby once the game is known and ready to start; a refusal opens none.</summary>
    private ILobby? OpenHostLobby(string gameId)
    {
        _joining = 0;
        _opening = gameId;
        if (!GameProfile.ListIds().Contains(gameId))
        {
            Refuse(new Refusal(RefusalKind.UnknownGame, gameId, gameId));
            return null;
        }
        var profile = GameProfile.Load(gameId);
        if (Preflight(profile, 0) is { } refusal)
        {
            Refuse(refusal);
            return null;
        }
        var hostOptions = GameOptions.HostValues(profile, ResolveGameDir(profile));
        return _options.Transport == TransportKind.Steam
            ? SteamLobby.Create(profile, hostOptions, ResolveGameDir(profile))
            : new LocalLobby((LocalTransport)_transport!, gameId, isHost: true, hostOptions);
    }

    private SteamLobby JoinLobby(ulong lobbyId)
    {
        _joining = lobbyId;
        _opening = null;
        return SteamLobby.Join(lobbyId);
    }

    /// <summary>The start checks (<see cref="StartChecks"/>), which a run that never starts the game (--no-launch) skips.</summary>
    private Refusal? Preflight(GameProfile profile, ulong lobbyId) =>
        _options.NoLaunch ? null : StartChecks.Run(profile, ResolveGameDir(profile), lobbyId);

    private void Open(Command command, string argument)
    {
        if (Reconnecting)
        {
            Log.Info("Already reconnecting");
            return;
        }
        if (_lobby != null)
        {
            Log.Info("Already in a lobby");
            return;
        }
        _refusal = null;
        _lobby = OpenLobby(command, argument);
    }

    private static string SoleProfileId()
    {
        var ids = GameProfile.ListIds();
        return ids.Count == 1 ? ids[0] : throw new ArgumentException("Several game profiles exist, pass --game <id>");
    }

    private int Loop()
    {
        while (!_stopRequested && (_interactive || _session?.Ended != true || Reconnecting))
        {
            RunCommands();
            if (!TryRejoin()) return 1;
            _steam?.Pump();
            if (_steam?.Lost == true) throw new SteamLostException();
            _transport!.Pump();
            _avatars?.Pump();
            AcceptInvite();
            _lobby?.Pump();
            if (_lobby?.Failure is { } failure && !OnLobbyFailure(failure)) return 1;
            DropEndedHeldSession();
            WatchHost();
            WaitForHost();
            ReopenLobby();
            FollowHost();
            RejoinHeldSession();
            if (!_sessionStarted && _lobby is { IsReady: true } && !StartSession() && !_interactive) return 1;
            _bridge?.Pump();
            _bridgeWasReady |= _bridge?.IsReady == true;
            _session?.Pump();
            _saveSync?.Pump();
            _logForwarder?.Pump();
            _crashSender?.Pump();
            _nativeJoin?.Pump();
            if (_launchPending && !TryLaunch())
            {
                if (!_interactive) return 1;
                EndSession();
            }
            if (_interactive && _session?.Ended == true) EndSession();
            PollWhileIdle();
            PublishStatus();
            Thread.Sleep(PumpIntervalMs);
        }
        return 0;
    }

    /// <summary>A lobby that failed: kept as a held session where there is one (App.Reconnect.cs), else the session ends with
    /// the reason, and a guest retries the lobby it was in. False when the CLI should exit.</summary>
    private bool OnLobbyFailure(LobbyFailure failure)
    {
        if (HoldSession(failure)) return true;
        Log.Info($"Lobby failure: {failure.Message}");
        var guestOf = _sessionStarted && _lobby!.OwnerId != _transport!.LocalId ? _lobby.Id : (ulong?)null;
        var gameId = _lobby!.GameId;
        var retrying = guestOf != null || _rejoin != null;
        if (!retrying) Refuse(failure.Kind, failure.Detail);
        EndSession();
        if (guestOf is { } lobbyId) _rejoin ??= new Rejoin(lobbyId) { GameId = gameId };
        return guestOf != null || _interactive;
    }

    /// <summary>Commands come from another thread; a failing one is logged and the loop carries on.</summary>
    private void RunCommands()
    {
        while (_commands.TryDequeue(out var command))
        {
            try
            {
                command();
            }
            catch (Exception e)
            {
                Log.Info($"Command failed: {e.Message}");
                EndSession();
            }
        }
    }

    private void AcceptInvite()
    {
        if (_steam?.PendingInvite is not { } invite) return;
        _steam.PendingInvite = null;
        if (_lobby?.Id == invite.LobbyId) return;
        if (IsIdle)
        {
            _refusal = null;
            _lobby = JoinLobby(invite.LobbyId);
        }
        else
        {
            _invite = new PendingInvite(invite.LobbyId, invite.FromId, _steam.PersonaName(invite.FromId));
            Log.Info($"Invite to lobby {invite.LobbyId} kept, already in a lobby");
        }
        InviteAccepted?.Invoke();
    }

    /// <summary>Starts the session of a ready lobby. False when the game cannot start here (a joined lobby's game is not ready
    /// or the bridge or save sync could not be built): the refusal is set and the lobby left.</summary>
    private bool StartSession()
    {
        var profile = GameProfile.Load(_lobby!.GameId!);
        var isHost = _lobby.OwnerId == _transport!.LocalId;
        if (!isHost && Preflight(profile, _lobby.Id) is { } refusal)
        {
            Refuse(refusal);
            EndSession();
            return false;
        }
        if (Reconnecting) Log.Event("Rejoined the session");
        CancelReconnect();
        _profile = profile;
        _gameDir = ResolveGameDir(profile);
        _hostId = _lobby.OwnerId;
        _startHeld = _interactive && !GameLauncher.IsRunning(profile);
        _sessionStarted = true;
        if (!isHost) Log.Event($"Joined {profile.Name} lobby");
        if (profile.RendezvousSession && _gameDir != null) _nativeJoin = NativeJoin.Create(profile, _gameDir, _lobby, _transport.LocalId);
        if (profile.Bridged(_lobby.HostOptions) && !StartBridgedSession())
        {
            EndSession();
            return false;
        }
        _follow = NewHostFollow();
        _launchPending = true;
        return true;
    }

    /// <summary>The bridge, frames between launchers, build check, save sync and log forwarding of a lobby game (or of a
    /// rendezvous game whose host chose the bridged transport). False, with the refusal set, when the loopback port is taken or
    /// the save sync cannot start; log forwarding is optional.</summary>
    private bool StartBridgedSession()
    {
        var port = _options.BridgePort != 0 ? _options.BridgePort : _profile!.Port;
        try
        {
            _bridge = new LoopbackBridge(_profile!, port);
        }
        catch (SocketException e)
        {
            Log.Info($"Could not open the loopback bridge on port {port}: {e.Message}");
            Refuse(RefusalKind.PortInUse, port.ToString());
            return false;
        }
        _session = new Session(_profile!, _lobby!, _transport!, _bridge);
        if (!_session.IsHost) _session.FileFrameReceived += OnFileFrame;
        _buildCheck = BuildCheck.Create(_profile!, _gameDir, _session, _transport!);
        if (_gameDir == null) return true;
        try
        {
            _saveSync = SaveSyncCoordinator.Create(_profile!, _gameDir, _options.SaveSource, _steam?.AccountId, _session, _transport!);
        }
        catch (Exception e) when (IsServiceFailure(e))
        {
            Log.Info($"Save sync could not start: {e.Message}");
            Refuse(RefusalKind.SaveFolderMissing);
            return false;
        }
        try
        {
            _logForwarder = LogForwarder.Create(_profile!, _gameDir, _session, _transport!);
        }
        catch (Exception e) when (IsServiceFailure(e))
        {
            Log.Info($"Log forwarding is off: {e.Message}");
        }
        _crashSender = CrashSender.Create(_profile!, _gameDir, _session, _transport!, _logForwarder);
        CrashReceiver.Attach(_gameDir, _session, _transport!, MemberName);
        return true;
    }

    private string MemberName(ulong id) => _lobby?.Members.FirstOrDefault(member => member.Id == id)?.Name ?? id.ToString();

    private static bool IsServiceFailure(Exception e) =>
        e is InvalidOperationException or ArgumentException or IOException or UnauthorizedAccessException;

    /// <summary>The save the host sends, as it arrives: the meter behind "Receiving save 40%".</summary>
    private void OnFileFrame(ulong sender, Frame frame)
    {
        if (frame.Type == Msg.FileBegin && FileMessages.TryParseBegin(frame.Payload, out var begin)) _saveMeter.OnBegin(begin.Id, begin.Name, begin.Size);
        else if (frame.Type == Msg.FileChunk && FileMessages.TryParseChunk(frame.Payload, out var chunk)) _saveMeter.OnChunk(chunk.Id, chunk.Data.Length);
    }

    /// <summary>Starts the game once build check and save sync are ready. False when the session cannot go on (the refusal is
    /// set): the builds differ, the save sync timed out, or the game could not start.</summary>
    private bool TryLaunch()
    {
        if (_buildCheck is { Mismatch: not null } refused)
        {
            Refuse(Refusals.FromBuildMismatch(refused.Reason), MismatchDetail(refused));
            return false;
        }
        if (_buildCheck?.Ready == false) return true;
        if (_saveSync?.TimedOut == true)
        {
            Log.Info("Save sync timed out, not launching the game");
            Refuse(RefusalKind.SaveTimedOut);
            return false;
        }
        if (_saveSync?.Ready == false || StartGateClosed()) return true;
        _launchPending = false;
        var options = AdapterOptions();
        _saveSync?.EnableAdapter(options);
        if (_options.NoLaunch) return true;
        Log.Event($"Starting {_profile!.Name}");
        var result = _profile.RendezvousSession
            ? _gameDir != null ? GameLauncher.LaunchWithOptions(_profile, _gameDir, options) : LaunchResult.SteamStartFailed
            : GameLauncher.Launch(_profile, _gameDir);
        if (result == LaunchResult.Started) _launchedAtMs = Environment.TickCount64;
        return result is LaunchResult.Started or LaunchResult.AlreadyRunning || RefuseLaunch(result);
    }

    /// <summary>The window's launchers wait for the start gate (<see cref="StartGate"/>): the host for its START GAME, a guest for the
    /// host's game to start. Once the host was seen starting, a guest stays open. A command-line run holds nothing.</summary>
    private bool StartGateClosed()
    {
        if (!_startHeld) return false;
        var isHost = _hostId == _transport!.LocalId;
        var hostStep = isHost ? SessionStep.None : HostStep();
        _hostStarted |= StartGate.HostStarted(hostStep);
        return !StartGate.Open(isHost, _startHeld, _startRequested, hostStep, _hostStarted);
    }

    private SessionStep HostStep() =>
        _lobby?.Members.FirstOrDefault(member => member.Id == _hostId) is { } host ? PublishedStep(_lobby, host).Step : SessionStep.None;

    /// <summary>The numbers the refusal for a build mismatch shows: the host's and this machine's.</summary>
    private string MismatchDetail(BuildCheck check) =>
        check.Compared is { } compared
            ? BuildInfo.Detail(check.Reason, compared.Host, compared.Local, _lobby?.Listing?.ModBuild ?? 0, ModInstaller.Status(_profile!, _gameDir).Build)
            : string.Empty;

    /// <summary>Sets the refusal for a game start that failed. Always false.</summary>
    private bool RefuseLaunch(LaunchResult result)
    {
        switch (result)
        {
            case LaunchResult.GameUnsupported:
                Refuse(RefusalKind.GameUnsupported, GameBuilds.Installed(_profile!, _gameDir).ToString());
                break;
            case LaunchResult.ModNotInstalled:
                Refuse(RefusalKind.ModNotInstalled);
                break;
            default:
                Refuse(RefusalKind.LaunchFailed);
                break;
        }
        return false;
    }

    private Dictionary<string, string> AdapterOptions() =>
        GameOptions.ForAdapter(_profile!, GameOptions.Current(_profile!, _gameDir), _lobby!.HostOptions, _hostId == _transport!.LocalId);

    /// <summary>Records why a start did not go on and tells the player once. The refusal names the game and the lobby being
    /// opened or left.</summary>
    private void Refuse(RefusalKind kind, string detail = "") =>
        Refuse(new Refusal(kind, detail, _lobby?.GameId ?? _profile?.Id ?? _opening, _lobby is { Id: not 0 } lobby ? lobby.Id : _joining));

    private void Refuse(Refusal refusal)
    {
        _refusal = refusal;
        Log.Event(StatusText.Refusal(refusal));
        PublishStep(LocalProgress());
    }

    /// <summary>Drops the lobby and everything built on it, leaving the app idle.</summary>
    private void EndSession()
    {
        DropSession();
        DisposeLobby();
        _view = null;
        _invite = null;
        _timedLobby = null;
        _lobbyOpenedAtMs = null;
        _lastListing = null;
        ResetAdapterSettings();
    }

    private void DisposeLobby()
    {
        _lobby?.Dispose();
        _lobby = null;
    }

    /// <summary>Drops the session and its services (bridge, save sync, log and crash forwarding) but not the lobby.</summary>
    private void DropSession()
    {
        _follow = null;
        _buildCheck = null;
        _logForwarder = null;
        _crashSender = null;
        _nativeJoin?.Dispose();
        _nativeJoin = null;
        _saveSync?.Dispose();
        _saveSync = null;
        _session?.Dispose();
        _session = null;
        _bridge?.Dispose();
        _bridge = null;
        _profile = null;
        _gameDir = null;
        _hostId = 0;
        _launchPending = false;
        _startHeld = false;
        _startRequested = false;
        _hostStarted = false;
        _hostStartedUnix = 0;
        _launchedAtMs = null;
        _bridgeWasReady = false;
        _saveMeter = new SaveProgressMeter();
        _sessionStarted = false;
    }

    private string? ResolveGameDir(GameProfile profile) => _options.GameDir ?? GameFolders.Find(profile);

    /// <summary>Crash recovery and cleanup: every profile with save sync goes back to coop=0 and loses its session folder.
    /// A game that is running keeps both (its session folder holds the saves it is playing from) and is reset once it exits.</summary>
    private void ResetAdapterSettings()
    {
        foreach (var id in GameProfile.ListIds()) ResetAdapterSettings(GameProfile.Load(id));
    }

    private void ResetAdapterSettings(GameProfile profile)
    {
        if (profile.SaveSync is not { } config || ResolveGameDir(profile) is not { } gameDir) return;
        if (GameLauncher.IsRunning(profile))
        {
            Log.Info($"{profile.Name} is running, its co-op settings are left as they are");
            _pendingResets.Add(profile.Id);
            return;
        }
        _pendingResets.Remove(profile.Id);
        try
        {
            AdapterSettings.Reset(gameDir, profile, config);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            Log.Info($"Could not reset {profile.Name}'s co-op settings: {e.Message}");
        }
    }

    /// <summary>Work that only matters at rest: the co-op settings of games that were running when a session ended, the friends
    /// hosting a lobby and, while the window watches the Lobbies tab, the lobby list.</summary>
    private void PollWhileIdle()
    {
        if (!IsIdle)
        {
            ClearDiscovery();
            return;
        }
        var now = Environment.TickCount64;
        if (_directory != null) PollDiscovery(_directory, now);
        if (_pendingResets.Count == 0 || now < _nextResetCheckMs) return;
        _nextResetCheckMs = now + PendingResetCheckMs;
        foreach (var id in _pendingResets.ToList())
        {
            var profile = GameProfile.Load(id);
            if (!GameLauncher.IsRunning(profile)) ResetAdapterSettings(profile);
        }
    }

    /// <summary>Friends' lobbies every <see cref="FriendPollMs"/>, the lobby list every <see cref="LobbySearchMs"/> while watched;
    /// the status rows are worked out again when either asked for or received something.</summary>
    private void PollDiscovery(LobbyDirectory directory, long now)
    {
        var changed = directory.Revision != _directoryRevision;
        if (now >= _nextFriendPollMs)
        {
            _nextFriendPollMs = now + FriendPollMs;
            _friendRows = FriendLobbies.Select(FriendLobbies.Read(), GameProfile.ListIds(), FriendLobbies.MaxFriendRows);
            directory.ReadFriends(_friendRows);
            changed = true;
        }
        if (_watchLobbies && now >= _nextLobbySearchMs)
        {
            _nextLobbySearchMs = now + LobbySearchMs;
            directory.Search();
        }
        if (!changed) return;
        _directoryRevision = directory.Revision;
        _friends = _friendRows.Select(friend => friend with { Listing = directory.FriendListing(friend.LobbyId) }).ToList();
        _lobbies = LobbyDirectory.Select(directory.Results, GameProfile.ListIds(), _transport!.LocalId);
    }

    /// <summary>A lobby is open or being rebuilt: nothing is offered, and the next idle moment looks again at once.</summary>
    private void ClearDiscovery()
    {
        _friendRows = [];
        _friends = [];
        _lobbies = [];
        _nextFriendPollMs = 0;
        _nextLobbySearchMs = 0;
        _directoryRevision = -1;
    }
}
