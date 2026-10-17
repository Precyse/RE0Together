using System.Collections.Concurrent;

namespace CoopLauncher;

/// <summary>
/// Wires transport, lobby, bridge and session together and runs the ~100 Hz main loop. The CLI runs one session and exits;
/// an interactive app (GUI) stays up, takes Host/Join/Leave commands and returns to idle when a session ends.
/// </summary>
public sealed class App
{
    private const int PumpIntervalMs = 10;

    private readonly CliOptions _options;
    private readonly bool _interactive;
    private readonly ConcurrentQueue<Action> _commands = new();
    private volatile bool _stopRequested;
    private SteamBootstrap? _steam;
    private ITransport? _transport;
    private ILobby? _lobby;
    private LoopbackBridge? _bridge;
    private Session? _session;
    private SaveSyncCoordinator? _saveSync;
    private LogForwarder? _logForwarder;
    private GameProfile? _profile;
    private string? _gameDir;
    private bool _launchPending;
    private BuildCheck? _buildCheck;
    private Rejoin? _rejoin;
    private HostFollow? _follow;
    private BuildMismatch _problem;

    public App(CliOptions options, bool interactive = false)
    {
        _options = options;
        _interactive = interactive;
    }

    public AppStatus Status { get; private set; } = AppStatus.Idle;

    /// <summary>Raised on the main loop thread when Status changes.</summary>
    public event Action<AppStatus>? StatusChanged;

    public void Stop() => _stopRequested = true;

    /// <summary>The window's loop host could not start the loop (Steam not running): shown until the loop runs again.</summary>
    public void MarkOffline()
    {
        if (Status.State == AppState.Offline) return;
        Status = new AppStatus(AppState.Offline);
        StatusChanged?.Invoke(Status);
    }

    public void Host(string gameId) => _commands.Enqueue(() => Open(Command.Host, gameId));

    public void Join(ulong lobbyId) => _commands.Enqueue(() => Open(Command.Join, lobbyId.ToString()));

    public void Leave() => _commands.Enqueue(() =>
    {
        _rejoin = null;
        EndSession();
    });

    public void Invite() => _commands.Enqueue(() =>
    {
        if (_lobby is not { IsReady: true } lobby)
        {
            Log.Info("No lobby to invite to");
            return;
        }
        _steam?.ShowInviteDialog(lobby.Id);
        Log.Info("Invite dialog opened");
    });

    public int Run()
    {
        _steam = _options.Transport == TransportKind.Steam ? new SteamBootstrap() : null;
        using var steam = _steam;
        _transport = _steam != null
            ? new SteamTransport(id => _lobby?.Members.Any(m => m.Id == id) == true || _session?.HasPeer(id) == true)
            : new LocalTransport(_options.LocalPort, _options.PeerPort);
        using var transport = _transport;
        ResetAdapterSettings();
        try
        {
            _lobby = OpenLobby(_options.Command, _options.Argument);
            return Loop();
        }
        finally
        {
            EndSession();
        }
    }

    private ILobby? OpenLobby(Command command, string? argument)
    {
        switch (command, _options.Transport)
        {
            case (Command.Host, TransportKind.Steam):
                return SteamLobby.Create(GameProfile.Load(argument!), HostOptionsOf(argument!));
            case (Command.Host, _):
                return new LocalLobby((LocalTransport)_transport!, argument!, isHost: true, HostOptionsOf(argument!));
            case (Command.Join, TransportKind.Steam):
                return SteamLobby.Join(ulong.Parse(argument!));
            case (Command.Join, _):
                return new LocalLobby((LocalTransport)_transport!, _options.Game ?? SoleProfileId(), isHost: false);
            default:
                Log.Info("Waiting for a Steam invite");
                return null;
        }
    }

    private Dictionary<string, string> HostOptionsOf(string gameId)
    {
        var profile = GameProfile.Load(gameId);
        return GameOptions.HostValues(profile, ResolveGameDir(profile));
    }

    private void Open(Command command, string argument)
    {
        if (_lobby != null)
        {
            Log.Info("Already in a lobby");
            return;
        }
        _problem = BuildMismatch.None;
        _lobby = OpenLobby(command, argument);
    }

    private static string SoleProfileId()
    {
        var ids = GameProfile.ListIds();
        return ids.Count == 1 ? ids[0] : throw new ArgumentException("Several game profiles exist, pass --game <id>");
    }

    private int Loop()
    {
        while (!_stopRequested && (_interactive || _session?.Ended != true || _rejoin != null))
        {
            RunCommands();
            if (!TryRejoin()) return 1;
            _steam?.Pump();
            _transport!.Pump();
            AcceptInvite();
            _lobby?.Pump();
            if (_lobby?.Failure is { } failure && !HoldGuestSession(failure))
            {
                Log.Info($"Lobby failure: {failure}");
                var guestOf = _session != null && _lobby.OwnerId != _transport!.LocalId ? _lobby.Id : (ulong?)null;
                EndSession();
                if (guestOf is { } lobbyId) _rejoin ??= new Rejoin(lobbyId);
                else if (!_interactive) return 1;
            }
            DropEndedHeldSession();
            FollowHost();
            RejoinHeldSession();
            if (_session == null && _lobby is { IsReady: true }) StartSession();
            _bridge?.Pump();
            _session?.Pump();
            _saveSync?.Pump();
            _logForwarder?.Pump();
            if (_launchPending && !TryLaunch())
            {
                if (!_interactive) return 1;
                EndSession();
            }
            if (_interactive && _session?.Ended == true) EndSession();
            PublishStatus();
            Thread.Sleep(PumpIntervalMs);
        }
        return 0;
    }

    /// <summary>A guest whose lobby object failed keeps its session, its game link and its services while the peers are held
    /// (Session.Hold); only the lobby is dropped and rejoined. False when there is no such session to hold.</summary>
    private bool HoldGuestSession(string failure)
    {
        if (_session is not { Ended: false, IsHost: false } session || !session.Hold()) return false;
        Log.Info($"Lobby failure: {failure}, keeping the session while rejoining");
        var lobbyId = session.LobbyId;
        _follow = null;
        _lobby!.Dispose();
        _lobby = null;
        _rejoin ??= new Rejoin(lobbyId);
        return true;
    }

    /// <summary>A held session whose grace ran out is gone: it is dropped while the rejoin runs, so the rejoined lobby starts fresh.</summary>
    private void DropEndedHeldSession()
    {
        if (_session is { Ended: true } && _rejoin != null) DropSession();
    }

    /// <summary>The rejoined lobby replaces the failed one in a held session when it is the same lobby with the same host;
    /// otherwise the session starts over on it.</summary>
    private void RejoinHeldSession()
    {
        if (_session is not { Held: true } session || _lobby is not { IsReady: true } lobby) return;
        if (lobby.Id == session.LobbyId && lobby.OwnerId == session.HostId)
        {
            session.Rebind(lobby);
            _rejoin = null;
            _follow = NewHostFollow();
        }
        else
        {
            DropSession();
        }
    }

    private HostFollow? NewHostFollow() =>
        _steam != null && _session is { IsHost: false } session ? new HostFollow(session.HostId, _profile!.Id) : null;

    /// <summary>A guest whose host opened a new lobby (it crashed or relaunched) leaves the old one and joins the new one,
    /// through the same retrying path as a lost connection.</summary>
    private void FollowHost()
    {
        if (_lobby == null || _follow?.MovedTo(_lobby) is not { } lobbyId) return;
        Log.Info($"The host opened lobby {lobbyId}, following");
        EndSession();
        _rejoin ??= new Rejoin(lobbyId);
    }

    /// <summary>Drives a pending rejoin. False when it gave up and the CLI should exit.</summary>
    private bool TryRejoin()
    {
        if (_rejoin == null || _lobby != null) return true;
        if (_rejoin.GaveUp)
        {
            Log.Info($"Could not rejoin lobby {_rejoin.LobbyId}, giving up");
            _rejoin = null;
            return _interactive;
        }
        if (_rejoin.AttemptDue()) _lobby = OpenLobby(Command.Join, _rejoin.LobbyId.ToString());
        return true;
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
        if (_steam?.PendingInviteLobby is not { } lobbyId) return;
        _steam.PendingInviteLobby = null;
        if (_lobby != null)
        {
            Log.Info("Already in a lobby, ignoring invite");
            return;
        }
        _problem = BuildMismatch.None;
        _lobby = SteamLobby.Join(lobbyId);
    }

    private void StartSession()
    {
        if (_rejoin != null) Log.Info("Rejoined the session");
        _rejoin = null;
        _profile = GameProfile.Load(_lobby!.GameId!);
        _gameDir = ResolveGameDir(_profile);
        _bridge = new LoopbackBridge(_profile, _options.BridgePort != 0 ? _options.BridgePort : _profile.Port);
        _session = new Session(_profile, _lobby, _transport!, _bridge);
        _buildCheck = BuildCheck.Create(_profile, _gameDir, _session, _transport!);
        if (_gameDir != null)
        {
            _saveSync = SaveSyncCoordinator.Create(_profile, _gameDir, _options.SaveSource, _steam?.AccountId, _session, _transport!);
            _logForwarder = LogForwarder.Create(_profile, _gameDir, _session, _transport!);
        }
        _follow = NewHostFollow();
        _launchPending = true;
    }

    /// <summary>Starts the game once save sync is ready. False when the save sync timed out.</summary>
    private bool TryLaunch()
    {
        if (_buildCheck is { Mismatch: not null } refused)
        {
            _problem = refused.Reason;
            return false;
        }
        if (_buildCheck?.Ready == false) return true;
        if (_saveSync?.TimedOut == true)
        {
            Log.Info("Save sync timed out, not launching the game");
            return false;
        }
        if (_saveSync?.Ready == false) return true;
        _launchPending = false;
        _saveSync?.EnableAdapter(GameOptions.Current(_profile!, _gameDir));
        return _options.NoLaunch || GameLauncher.Launch(_profile!, _gameDir);
    }

    /// <summary>Drops the lobby and everything built on it, leaving the app idle.</summary>
    private void EndSession()
    {
        DropSession();
        _lobby?.Dispose();
        _lobby = null;
        ResetAdapterSettings();
    }

    /// <summary>Drops the session and its services (bridge, save sync, log forwarder) but not the lobby.</summary>
    private void DropSession()
    {
        _follow = null;
        _buildCheck = null;
        _logForwarder = null;
        _saveSync?.Dispose();
        _saveSync = null;
        _session?.Dispose();
        _session = null;
        _bridge?.Dispose();
        _bridge = null;
        _profile = null;
        _gameDir = null;
        _launchPending = false;
    }

    private void PublishStatus()
    {
        var status = CurrentStatus();
        if (status == Status) return;
        Status = status;
        StatusChanged?.Invoke(status);
    }

    private AppStatus CurrentStatus()
    {
        if (_lobby == null) return _session is { Held: true } ? Status : AppStatus.Idle with { Problem = _problem };
        if (!_lobby.IsReady) return new AppStatus(AppState.Connecting);
        var state = _session is { PeerConnected: true } ? AppState.PeerConnected
            : _bridge?.IsReady == true ? AppState.GameRunning
            : _lobby.OwnerId == _transport!.LocalId ? AppState.Hosting
            : AppState.Joined;
        return new AppStatus(state, _lobby.Id, _session?.RttMs)
        {
            Players = PlayerSlots(_lobby), GameId = _lobby.GameId, SeatCharacters = SeatCharacters(_lobby),
        };
    }

    private IReadOnlyList<PlayerSlot> PlayerSlots(ILobby lobby) =>
        lobby.Members
            .OrderByDescending(member => member.Id == lobby.OwnerId)
            .Select(member => new PlayerSlot(member.Name, member.Id == lobby.OwnerId, member.Id == _transport!.LocalId))
            .ToList();

    private List<string> SeatCharacters(ILobby lobby) =>
        lobby.GameId is { } id ? GameOptions.SeatCharacters(GameProfile.Load(id), lobby.HostOptions) : [];

    private string? ResolveGameDir(GameProfile profile) => _options.GameDir ?? GameFolders.Find(profile);

    /// <summary>Crash recovery and cleanup: every profile with save sync goes back to coop=0 and loses its session folder.
    /// A game that is running keeps both: its session folder holds the saves it is playing from.</summary>
    private void ResetAdapterSettings()
    {
        foreach (var id in GameProfile.ListIds())
        {
            var profile = GameProfile.Load(id);
            if (profile.SaveSync is not { } config || ResolveGameDir(profile) is not { } gameDir) continue;
            if (GameLauncher.IsRunning(profile))
            {
                Log.Info($"{profile.Name} is running, its co-op settings are left as they are");
                continue;
            }
            try
            {
                AdapterSettings.Reset(gameDir, config);
            }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException)
            {
                Log.Info($"Could not reset {profile.Name}'s co-op settings: {e.Message}");
            }
        }
    }
}
