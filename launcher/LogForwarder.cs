namespace CoopLauncher;

/// <summary>
/// Guests mirror the lines their adapter appends to its log to the host (LOG_APPEND), paced to one chunk every
/// <see cref="SendIntervalMs"/> until <see cref="Flush"/> lifts the pace (a crash or exit: <see cref="CrashSender"/>). The host appends
/// them to one file per peer and session (peer_&lt;id&gt;_&lt;stamp&gt;.log beside its own adapter log, the newest <see cref="KeptPeerLogs"/>
/// per peer are kept). A disk error on either side skips that pass and never reaches the loop.
/// </summary>
public sealed class LogForwarder
{
    /// <summary>Peer log files kept per peer on the host.</summary>
    public const int KeptPeerLogs = 10;

    private const long SendIntervalMs = 2000;
    private const int MaxBytesPerSend = 32 * 1024;
    private const string PeerPrefix = "peer_";
    private const string LogExtension = ".log";

    private readonly string _logPath;
    private readonly string _logDir;
    private readonly ITransport _transport;
    private readonly Func<ulong> _hostId;
    private readonly bool _isHost;
    private readonly string _stamp = DateTime.Now.ToString(AppData.StampFormat);
    private readonly HashSet<ulong> _prunedPeers = [];
    private bool _failureLogged;
    private long _offset;
    private long _lastSendMs;
    private bool _flushing;

    internal LogForwarder(string logPath, ITransport transport, Func<ulong> hostId, bool isHost)
    {
        _logPath = logPath;
        _logDir = Path.GetDirectoryName(logPath)!;
        _transport = transport;
        _hostId = hostId;
        _isHost = isHost;
        _offset = isHost ? 0 : LogLength();
    }

    /// <summary>Null when the profile names no adapter log.</summary>
    public static LogForwarder? Create(GameProfile profile, string gameDir, Session session, ITransport transport)
    {
        if (profile.AdapterLog is not { } log) return null;
        var forwarder = new LogForwarder(Path.Combine(gameDir, log), transport, () => session.HostId, session.IsHost);
        session.DiagnosticsFrameReceived += forwarder.OnFrame;
        return forwarder;
    }

    public void Pump()
    {
        if (_isHost) return;
        try
        {
            var now = Environment.TickCount64;
            if (!_flushing && now - _lastSendMs < SendIntervalMs) return;
            _lastSendMs = now;
            SendLogLines();
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            LogFailureOnce($"the adapter log could not be read ({e.Message})");
        }
    }

    /// <summary>Sends the rest of the log on the next pumps without the pace, until the host has all of it. Guest only.</summary>
    public void Flush() => _flushing = !_isHost;

    /// <summary>The names of one peer's log files (the older peer_&lt;id&gt;.log and the per-session peer_&lt;id&gt;_&lt;stamp&gt;.log), not its crash files.</summary>
    public static bool IsPeerLog(string fileName, ulong peer) =>
        fileName.EndsWith(LogExtension, StringComparison.OrdinalIgnoreCase)
        && (fileName.Equals($"{PeerPrefix}{peer}{LogExtension}", StringComparison.OrdinalIgnoreCase)
            || fileName.StartsWith($"{PeerPrefix}{peer}_", StringComparison.OrdinalIgnoreCase));

    /// <summary>Of one peer's log file names, those beyond the newest <see cref="KeptPeerLogs"/> (the stamps sort by time; the older
    /// unstamped name sorts first).</summary>
    public static List<string> ExpiredPeerLogs(IEnumerable<string> peerLogNames) =>
        peerLogNames.OrderDescending(StringComparer.OrdinalIgnoreCase).Skip(KeptPeerLogs).ToList();

    private void SendLogLines()
    {
        do
        {
            var length = LogLength();
            if (length < _offset) _offset = 0;
            if (length == _offset)
            {
                _flushing = false;
                return;
            }
            var chunk = ReadFrom(_offset, (int)Math.Min(MaxBytesPerSend, length - _offset));
            if (chunk.Length == 0 || !_transport.Send(_hostId(), Framing.Reliable(Msg.LogAppend, chunk))) return;
            _offset += chunk.Length;
        }
        while (_flushing);
    }

    private void OnFrame(ulong sender, Frame frame)
    {
        if (!_isHost || frame.Type != Msg.LogAppend || frame.Payload.Length == 0) return;
        try
        {
            Directory.CreateDirectory(_logDir);
            AppendPeerLog(sender, frame.Payload);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            LogFailureOnce($"the log of {sender} could not be written ({e.Message})");
        }
    }

    private void AppendPeerLog(ulong sender, byte[] lines)
    {
        using (var file = new FileStream(Path.Combine(_logDir, $"{PeerPrefix}{sender}_{_stamp}{LogExtension}"), FileMode.Append, FileAccess.Write, FileShare.ReadWrite))
            file.Write(lines);
        if (_prunedPeers.Add(sender)) PrunePeerLogs(sender);
    }

    private void PrunePeerLogs(ulong peer)
    {
        var names = Directory.GetFiles(_logDir, $"{PeerPrefix}{peer}*{LogExtension}").Select(Path.GetFileName).OfType<string>().Where(name => IsPeerLog(name, peer));
        foreach (var expired in ExpiredPeerLogs(names))
        {
            try
            {
                File.Delete(Path.Combine(_logDir, expired));
            }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException)
            {
                Log.Info($"Could not delete the old peer log {expired}: {e.Message}");
            }
        }
    }

    private void LogFailureOnce(string what)
    {
        if (_failureLogged) return;
        _failureLogged = true;
        Log.Info($"Diagnostics: {what}, skipping");
    }

    private long LogLength() => File.Exists(_logPath) ? new FileInfo(_logPath).Length : 0;

    private byte[] ReadFrom(long offset, int count)
    {
        using var file = new FileStream(_logPath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
        file.Seek(offset, SeekOrigin.Begin);
        var buffer = new byte[count];
        var read = file.ReadAtLeast(buffer, count, throwOnEndOfStream: false);
        return read == count ? buffer : buffer[..read];
    }
}
