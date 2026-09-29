namespace CoopLauncher;

/// <summary>
/// Guests stream the lines their adapter appends to its log to the host (LOG_APPEND); the host appends them to
/// one file per peer next to its own adapter log.
/// </summary>
public sealed class LogForwarder
{
    private const long SendIntervalMs = 2000;
    private const int MaxBytesPerSend = 32 * 1024;
    private const string PeerLogPrefix = "peer_";

    private readonly string _logPath;
    private readonly ITransport _transport;
    private readonly Func<ulong> _hostId;
    private readonly bool _isHost;
    private long _offset;
    private long _lastSendMs;

    private LogForwarder(string logPath, ITransport transport, Func<ulong> hostId, bool isHost)
    {
        _logPath = logPath;
        _transport = transport;
        _hostId = hostId;
        _isHost = isHost;
        _offset = isHost ? 0 : LogLength();
    }

    /// <summary>Null when the profile names no adapter log.</summary>
    public static LogForwarder? Create(GameProfile profile, string gameDir, Session session, ITransport transport, ILobby lobby)
    {
        if (profile.AdapterLog is not { } log) return null;
        var forwarder = new LogForwarder(Path.Combine(gameDir, log), transport, () => lobby.OwnerId, lobby.OwnerId == transport.LocalId);
        session.LogFrameReceived += forwarder.OnFrame;
        return forwarder;
    }

    public void Pump()
    {
        var now = Environment.TickCount64;
        if (_isHost || now - _lastSendMs < SendIntervalMs) return;
        _lastSendMs = now;
        var length = LogLength();
        if (length < _offset) _offset = 0;
        if (length == _offset) return;
        var chunk = ReadFrom(_offset, (int)Math.Min(MaxBytesPerSend, length - _offset));
        if (chunk.Length > 0 && _transport.Send(_hostId(), new Frame(Msg.LogAppend, Framing.FlagReliable, 0, chunk)))
            _offset += chunk.Length;
    }

    private void OnFrame(ulong sender, Frame frame)
    {
        if (!_isHost || frame.Payload.Length == 0) return;
        var peerLog = Path.Combine(Path.GetDirectoryName(_logPath)!, $"{PeerLogPrefix}{sender}.log");
        using var file = new FileStream(peerLog, FileMode.Append, FileAccess.Write, FileShare.ReadWrite);
        file.Write(frame.Payload);
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
