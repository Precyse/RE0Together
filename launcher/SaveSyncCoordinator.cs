using System.Diagnostics;

namespace CoopLauncher;

/// <summary>Save sync for one session: the host sends the profile files to each new peer, the guest collects them and gates the game launch.
/// A host whose profile sends by pattern also watches its save folder and sends every save its game writes later; the guest
/// stages those and moves them into its session folder while its game is not running.</summary>
public sealed class SaveSyncCoordinator : IDisposable
{
    private const long TimeoutMs = 60_000;
    private const long GameCheckIntervalMs = 1000;
    private const string StagingFolder = "staging";

    private readonly SaveSyncProfile _config;
    private readonly string _gameDir;
    private readonly SaveSender? _sender;
    private readonly SaveReceiver? _receiver;
    private readonly SaveWatcher? _watcher;
    private readonly Func<IEnumerable<ulong>> _peers;
    private readonly string _gameProcess;
    private readonly long _startedMs = Environment.TickCount64;
    private long _lastGameCheckMs;

    private SaveSyncCoordinator(
        SaveSyncProfile config, string gameDir, string gameExe, SaveSender? sender, SaveReceiver? receiver, SaveWatcher? watcher,
        Func<IEnumerable<ulong>> peers)
    {
        _config = config;
        _gameDir = gameDir;
        _gameProcess = Path.GetFileNameWithoutExtension(gameExe);
        _sender = sender;
        _receiver = receiver;
        _watcher = watcher;
        _peers = peers;
    }

    /// <summary>Guests wait for every file; the host has nothing to wait for.</summary>
    public bool Ready => _receiver?.Complete ?? true;

    public bool TimedOut => !Ready && Environment.TickCount64 - _startedMs > TimeoutMs;

    /// <summary>Null when the profile has no save sync.</summary>
    public static SaveSyncCoordinator? Create(
        GameProfile profile, string gameDir, string? saveSource, uint? steamAccountId, Session session, ITransport transport, ILobby lobby)
    {
        if (profile.SaveSync is not { } config) return null;
        if (lobby.OwnerId == transport.LocalId)
        {
            var source = saveSource
                         ?? (config.HostSaveDir is { } hostDir ? SavePaths.Expand(hostDir) : null)
                         ?? (steamAccountId is { } account ? SteamLibrary.UserRemoteDir(profile.SteamAppId, account) : null);
            if (source == null) throw new ArgumentException("Local transport needs --save-source to host a save sync");
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
            return new SaveSyncCoordinator(config, gameDir, profile.Exe, sender, null, WatchSaves(config, source), () => session.PeerIds);
        }
        var filesDir = Path.Combine(gameDir, config.GuestSaveDir is { } guestDir ? SavePaths.Expand(guestDir) : config.SessionDir);
        var stagingDir = Path.Combine(gameDir, config.SessionDir, StagingFolder);
        var receiver = new SaveReceiver(config, filesDir, stagingDir, transport, () => lobby.OwnerId);
        session.FileFrameReceived += receiver.OnFrame;
        return new SaveSyncCoordinator(config, gameDir, profile.Exe, null, receiver, null, () => []);
    }

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
        if (!GameRunning()) _receiver.PromoteStaged();
    }

    private bool GameRunning()
    {
        var processes = Process.GetProcessesByName(_gameProcess);
        foreach (var process in processes) process.Dispose();
        return processes.Length > 0;
    }

    public void EnableAdapter() => AdapterSettings.EnableCoop(_gameDir, _config);

    public void Dispose()
    {
        _watcher?.Dispose();
        _receiver?.Abort();
    }
}
