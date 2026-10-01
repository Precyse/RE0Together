namespace CoopLauncher;

/// <summary>Save sync for one session: the host sends the profile files to each new peer, the guest collects them and gates the game launch.</summary>
public sealed class SaveSyncCoordinator : IDisposable
{
    private const long TimeoutMs = 60_000;

    private readonly SaveSyncProfile _config;
    private readonly string _gameDir;
    private readonly SaveSender? _sender;
    private readonly SaveReceiver? _receiver;
    private readonly long _startedMs = Environment.TickCount64;

    private SaveSyncCoordinator(SaveSyncProfile config, string gameDir, SaveSender? sender, SaveReceiver? receiver)
    {
        _config = config;
        _gameDir = gameDir;
        _sender = sender;
        _receiver = receiver;
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
            return new SaveSyncCoordinator(config, gameDir, sender, null);
        }
        var filesDir = Path.Combine(gameDir, config.GuestSaveDir is { } guestDir ? SavePaths.Expand(guestDir) : config.SessionDir);
        var receiver = new SaveReceiver(config, filesDir, transport, () => lobby.OwnerId);
        session.FileFrameReceived += receiver.OnFrame;
        return new SaveSyncCoordinator(config, gameDir, null, receiver);
    }

    public void Pump() => _sender?.Pump();

    public void EnableAdapter() => AdapterSettings.EnableCoop(_gameDir, _config);

    public void Dispose() => _receiver?.Abort();
}
