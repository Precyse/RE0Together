namespace CoopLauncher;

/// <summary>Guest side of crash reporting: watches the profile's crash-file locations and the game's process, and sends the host what a
/// crash or hang leaves behind through the shared <see cref="FileStreamer"/> (FILE_* frames, the host's <see cref="CrashReceiver"/> ACKs):
/// the new stable crash files, and the whole adapter log once the game has exited or stalled. At the same time it lifts the pace of the
/// log mirror (<see cref="LogForwarder.Flush"/>). The rules of what goes when are <see cref="CrashQueue"/> and <see cref="CrashIncidents"/>.
/// A disk error skips that pass. Nothing is sent when the guest's launcher is gone too.</summary>
public sealed class CrashSender
{
    private const long PollIntervalMs = 1000;

    private readonly GameProfile _profile;
    private readonly string _gameDir;
    private readonly Func<ulong> _hostId;
    private readonly LogForwarder? _logs;
    private readonly CrashQueue _queue;
    private readonly CrashIncidents _incidents = new(() => DateTime.Now.ToString(AppData.StampFormat));
    private long _lastPollMs;
    private bool _capLogged;

    internal CrashSender(GameProfile profile, string gameDir, Func<ulong> hostId, ITransport transport, LogForwarder? logs, DateTime sessionStartUtc)
    {
        _queue = new CrashQueue(sessionStartUtc);
        _profile = profile;
        _gameDir = gameDir;
        _hostId = hostId;
        _logs = logs;
        Streamer = new FileStreamer(transport, "Crash files");
    }

    internal FileStreamer Streamer { get; }

    /// <summary>Null on the host and for a profile that names no crash files.</summary>
    public static CrashSender? Create(GameProfile profile, string gameDir, Session session, ITransport transport, LogForwarder? logs)
    {
        if (session.IsHost || profile.CrashFiles is not { Count: > 0 }) return null;
        var sender = new CrashSender(profile, gameDir, () => session.HostId, transport, logs, DateTime.UtcNow);
        session.FileFrameReceived += sender.OnFrame;
        return sender;
    }

    public void Pump()
    {
        Streamer.Pump();
        var now = Environment.TickCount64;
        if (now - _lastPollMs < PollIntervalMs) return;
        _lastPollMs = now;
        Poll(now, GameLauncher.Probe(_profile));
    }

    /// <summary>One look at the game and its crash files: opens or feeds an incident and queues what it takes.</summary>
    internal void Poll(long nowMs, GameProcess process)
    {
        var scan = _queue.Take(nowMs, ScanFiles());
        foreach (var path in scan.TooLarge) Log.Info($"Crash files: {path} is over {CrashRules.MaxFileBytes} bytes, not sent");
        if (_incidents.Next(nowMs, scan.Ready.Count > 0, process) is not { } incident)
        {
            if (_incidents.CapReached && !_capLogged)
            {
                _capLogged = true;
                Log.Info($"Crash files: {CrashRules.MaxIncidents} incidents already sent in this session, no more");
            }
            return;
        }
        if (incident.Opened || incident.SendLog) _logs?.Flush();
        foreach (var path in scan.Ready) Queue(incident.Stamp, path);
        if (incident.SendLog && _profile.AdapterLog is { } log) Queue(incident.Stamp, Path.Combine(_gameDir, log));
    }

    private List<CrashFileInfo> ScanFiles()
    {
        var found = new List<CrashFileInfo>();
        foreach (var spec in _profile.CrashFiles!)
        {
            if (!CrashRules.TrySplitSpec(spec, out var directory, out var pattern)) continue;
            try
            {
                var folder = Path.Combine(_gameDir, directory);
                if (!Directory.Exists(folder)) continue;
                found.AddRange(Directory.GetFiles(folder, pattern).Select(path => new FileInfo(path))
                    .Select(info => new CrashFileInfo(info.FullName, info.LastWriteTimeUtc, info.Length)));
            }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException)
            {
                Log.Info($"Crash files: {spec} cannot be read ({e.Message}), skipping");
            }
        }
        return found;
    }

    private void Queue(string stamp, string path)
    {
        var name = CrashRules.WireName(stamp, CrashRules.FlatName(Path.GetRelativePath(_gameDir, path)));
        if (!FileMessages.IsWireName(name))
        {
            Log.Info($"Crash files: {path} has no name the wire can carry, not sent");
            return;
        }
        try
        {
            using var file = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
            if (file.Length > CrashRules.MaxFileBytes)
            {
                Log.Info($"Crash files: {path} is over {CrashRules.MaxFileBytes} bytes, not sent");
                return;
            }
            var data = new byte[file.Length];
            file.ReadExactly(data);
            Streamer.Enqueue(_hostId(), name, data);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            Log.Info($"Crash files: {path} could not be read ({e.Message}), not sent");
        }
    }

    private void OnFrame(ulong sender, Frame frame)
    {
        if (frame.Type == Msg.FileAck && sender == _hostId()) Streamer.OnAck(sender, frame);
    }
}
