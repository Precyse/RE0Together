namespace CoopLauncher;

/// <summary>When a guest gives up waiting for the host's save set: nothing from the host at all within
/// <see cref="NoAnswerTimeoutMs"/> of the start, or no new frame for <see cref="StallTimeoutMs"/> once frames have arrived.</summary>
public static class SaveSyncTimeout
{
    public const long NoAnswerTimeoutMs = 60_000;
    public const long StallTimeoutMs = 30_000;

    /// <param name="startMs">When the sync was created.</param>
    /// <param name="firstFrameMs">When the host's first save frame arrived, or null while none has.</param>
    /// <param name="lastFrameMs">When the newest one arrived (ignored while there is no first frame).</param>
    /// <param name="ready">The set is complete: nothing to wait for.</param>
    public static bool TimedOut(long startMs, long? firstFrameMs, long lastFrameMs, bool ready, long nowMs)
    {
        if (ready) return false;
        return firstFrameMs == null ? nowMs - startMs > NoAnswerTimeoutMs : nowMs - lastFrameMs > StallTimeoutMs;
    }
}

/// <summary>Save sync for one session: the host sends the profile files to each new peer, the guest collects them and gates the game launch.
/// A host whose profile sends by pattern also watches its save folder and sends every save its game writes later; the guest
/// stages those and moves them into its session folder while its game is not running.</summary>
public sealed class SaveSyncCoordinator : IDisposable
{
    private const long GameCheckIntervalMs = 1000;

    private readonly GameProfile _profile;
    private readonly string _gameDir;
    private readonly SaveSender? _sender;
    private readonly SaveReceiver? _receiver;
    private readonly SaveWatcher? _watcher;
    private readonly Func<IEnumerable<ulong>> _peers;
    private readonly Func<ulong> _hostId;
    private readonly long _startedMs = Environment.TickCount64;
    private long? _firstFrameMs;
    private long _lastFrameMs;
    private long _lastGameCheckMs;

    private SaveSyncCoordinator(
        GameProfile profile, string gameDir, SaveSender? sender, SaveReceiver? receiver, SaveWatcher? watcher,
        Func<IEnumerable<ulong>> peers, Func<ulong> hostId)
    {
        _profile = profile;
        _gameDir = gameDir;
        _sender = sender;
        _receiver = receiver;
        _watcher = watcher;
        _peers = peers;
        _hostId = hostId;
    }

    /// <summary>Guests wait for every file; the host has nothing to wait for.</summary>
    public bool Ready => _receiver?.Complete ?? true;

    public bool TimedOut => SaveSyncTimeout.TimedOut(_startedMs, _firstFrameMs, _lastFrameMs, Ready, Environment.TickCount64);

    /// <summary>Null when the profile has no save sync. Throws <see cref="InvalidOperationException"/> when the save folder cannot be
    /// found on this machine (the caller refuses the session).</summary>
    public static SaveSyncCoordinator? Create(
        GameProfile profile, string gameDir, string? saveSource, uint? steamAccountId, Session session, ITransport transport)
    {
        if (profile.SaveSync is not { } config) return null;
        if (session.IsHost)
        {
            var source = saveSource ?? HostSaveDir(profile, config, steamAccountId)
                         ?? throw new InvalidOperationException($"No host save folder for {profile.Name} (local transport needs --save-source, the folder template needs a signed-in Steam user)");
            var sender = new SaveSender(config, source, transport);
            session.PeerJoined += sender.SendTo;
            session.SaveChanged += () =>
            {
                foreach (var peer in session.PeerIds) sender.SendTo(peer);
            };
            session.FileFrameReceived += (peer, frame) =>
            {
                if (frame.Type == Msg.FileAck) sender.OnAck(peer, frame);
            };
            return new SaveSyncCoordinator(profile, gameDir, sender, null, WatchSaves(config, source), () => session.PeerIds, () => session.HostId);
        }
        var guestDir = config.GuestSaveDir is { } template
            ? SavePaths.Expand(template) ?? throw new InvalidOperationException($"No guest save folder for {profile.Name} (the folder template needs a signed-in Steam user)")
            : config.SessionDir;
        var stagingDir = Path.Combine(gameDir, config.SessionDir, SaveReceiver.StagingFolder);
        var receiver = new SaveReceiver(config, Path.Combine(gameDir, guestDir), stagingDir, transport, () => session.HostId, GameLauncher.IsRunning(profile));
        var coordinator = new SaveSyncCoordinator(profile, gameDir, null, receiver, null, () => [], () => session.HostId);
        session.FileFrameReceived += coordinator.OnFileFrame;
        return coordinator;
    }

    /// <summary>The host's save folder from the profile's template, else the Steam cloud cache; null when neither is known here.</summary>
    internal static string? HostSaveDir(GameProfile profile, SaveSyncProfile config, uint? steamAccountId) =>
        config.HostSaveDir is { } template
            ? SavePaths.Expand(template)
            : steamAccountId is { } account ? SteamLibrary.UserRemoteDir(profile.SteamAppId, account) : null;

    /// <summary>Watches the host's save folder for saves written after the join; only profiles that send by pattern (the
    /// save names change, so a fixed list would miss them).</summary>
    private static SaveWatcher? WatchSaves(SaveSyncProfile config, string saveDir)
    {
        if (config.FilePattern == null) return null;
        if (!Directory.Exists(saveDir))
        {
            Log.Info($"Save sync: {saveDir} does not exist, later saves are not watched");
            return null;
        }
        return new SaveWatcher(saveDir, config.FilePattern);
    }

    public void Pump()
    {
        _sender?.Pump();
        SendSettledSaves();
        PromoteStagedWhenGameStopped();
    }

    private void OnFileFrame(ulong sender, Frame frame)
    {
        if (sender == _hostId())
        {
            var now = Environment.TickCount64;
            _firstFrameMs ??= now;
            _lastFrameMs = now;
        }
        _receiver!.OnFrame(sender, frame);
    }

    private void SendSettledSaves()
    {
        if (_watcher == null || _sender == null) return;
        foreach (var name in _watcher.TakeSettled())
        {
            var sent = true;
            foreach (var peer in _peers()) sent &= _sender.SendChanged(peer, name);
            if (!sent) _watcher.Touch(name);
        }
    }

    private void PromoteStagedWhenGameStopped()
    {
        var now = Environment.TickCount64;
        if (_receiver == null || now - _lastGameCheckMs < GameCheckIntervalMs) return;
        _lastGameCheckMs = now;
        if (!GameLauncher.IsRunning(_profile)) _receiver.PromoteStaged();
    }

    public void EnableAdapter(IReadOnlyDictionary<string, string> options) => AdapterSettings.EnableCoop(_gameDir, _profile, options);

    public void Dispose()
    {
        _watcher?.Dispose();
        _receiver?.Abort();
    }
}
