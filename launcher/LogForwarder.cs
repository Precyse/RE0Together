using System.Buffers.Binary;
using System.Text;

namespace CoopLauncher;

/// <summary>
/// Guests send their diagnostics to the host: the lines their adapter appends to its log (LOG_APPEND) and any
/// crash dump the adapter writes during the session (CRASH_DUMP). The host appends log lines to one file per peer and
/// session (peer_&lt;id&gt;_&lt;stamp&gt;.log, the newest <see cref="KeptPeerLogs"/> per peer are kept) and writes each dump as
/// peer_&lt;id&gt;_&lt;name&gt;, both next to its own adapter log. A disk error on either side skips that pass and never reaches the loop.
/// </summary>
public sealed class LogForwarder
{
    /// <summary>Peer log files kept per peer on the host.</summary>
    public const int KeptPeerLogs = 10;

    private const long SendIntervalMs = 2000;
    private const int MaxBytesPerSend = 32 * 1024;
    private const string PeerPrefix = "peer_";
    private const string DumpPattern = "crash-*.dmp";
    private const string LogExtension = ".log";
    private const int DumpHeaderFixedSize = 1 + 4;  // u8 name length, u32 offset (the name sits between them)

    private sealed class DumpTransfer
    {
        public required string Name { get; init; }
        public required byte[] Data { get; init; }
        public int Offset { get; set; }
    }

    private readonly string _logPath;
    private readonly string _logDir;
    private readonly ITransport _transport;
    private readonly Func<ulong> _hostId;
    private readonly bool _isHost;
    private readonly HashSet<string> _knownDumps;
    private readonly string _stamp = DateTime.Now.ToString(AppData.StampFormat);
    private readonly HashSet<ulong> _prunedPeers = [];
    private bool _failureLogged;
    private long _offset;
    private long _lastSendMs;
    private DumpTransfer? _dump;

    private LogForwarder(string logPath, ITransport transport, Func<ulong> hostId, bool isHost)
    {
        _logPath = logPath;
        _logDir = Path.GetDirectoryName(logPath)!;
        _transport = transport;
        _hostId = hostId;
        _isHost = isHost;
        _offset = isHost ? 0 : LogLength();
        _knownDumps = isHost ? [] : DumpNames();
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
            SendDumpChunk();
            var now = Environment.TickCount64;
            if (now - _lastSendMs < SendIntervalMs) return;
            _lastSendMs = now;
            StartNewDump();
            SendLogLines();
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            LogFailureOnce($"the adapter log could not be read ({e.Message})");
        }
    }

    /// <summary>The names of one peer's log files (the older peer_&lt;id&gt;.log and the per-session peer_&lt;id&gt;_&lt;stamp&gt;.log), not its dumps.</summary>
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
        var length = LogLength();
        if (length < _offset) _offset = 0;
        if (length == _offset) return;
        var chunk = ReadFrom(_offset, (int)Math.Min(MaxBytesPerSend, length - _offset));
        if (chunk.Length > 0 && _transport.Send(_hostId(), Framing.Reliable(Msg.LogAppend, chunk)))
            _offset += chunk.Length;
    }

    // A dump appears when the game crashes; it is complete once the adapter's crash handler has returned.
    private void StartNewDump()
    {
        if (_dump != null) return;
        foreach (var name in DumpNames())
        {
            if (_knownDumps.Contains(name)) continue;
            byte[] data;
            try
            {
                data = File.ReadAllBytes(Path.Combine(_logDir, name));
            }
            catch (IOException)
            {
                return;  // still being written by the crash handler; try again on the next pass
            }
            _knownDumps.Add(name);
            _dump = new DumpTransfer { Name = name, Data = data };
            Log.Info($"Sending crash dump {name} ({_dump.Data.Length} bytes) to the host");
            return;
        }
    }

    // A refused send (full buffer) is retried on the next pump; the offset only advances on acceptance.
    private void SendDumpChunk()
    {
        if (_dump == null) return;
        var name = Encoding.ASCII.GetBytes(_dump.Name);
        var length = Math.Min(MaxBytesPerSend, _dump.Data.Length - _dump.Offset);
        var payload = new byte[DumpHeaderFixedSize + name.Length + length];
        payload[0] = (byte)name.Length;
        name.CopyTo(payload, 1);
        BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(1 + name.Length), (uint)_dump.Offset);
        _dump.Data.AsSpan(_dump.Offset, length).CopyTo(payload.AsSpan(DumpHeaderFixedSize + name.Length));
        if (!_transport.Send(_hostId(), Framing.Reliable(Msg.CrashDump, payload))) return;
        _dump.Offset += length;
        if (_dump.Offset >= _dump.Data.Length) _dump = null;
    }

    private void OnFrame(ulong sender, Frame frame)
    {
        if (!_isHost || frame.Payload.Length == 0) return;
        try
        {
            Directory.CreateDirectory(_logDir);
            if (frame.Type == Msg.LogAppend) AppendPeerLog(sender, frame.Payload);
            else WriteDumpChunk(sender, frame.Payload);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            LogFailureOnce($"a diagnostics file from {sender} could not be written ({e.Message})");
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

    private void WriteDumpChunk(ulong sender, byte[] payload)
    {
        int nameLength = payload[0];
        if (payload.Length < DumpHeaderFixedSize + nameLength) return;
        var name = Encoding.ASCII.GetString(payload, 1, nameLength);
        if (Path.GetFileName(name) != name || !name.EndsWith(".dmp", StringComparison.OrdinalIgnoreCase)) return;
        var offset = BinaryPrimitives.ReadUInt32LittleEndian(payload.AsSpan(1 + nameLength));
        var path = Path.Combine(_logDir, $"{PeerPrefix}{sender}_{name}");
        using var file = new FileStream(path, offset == 0 ? FileMode.Create : FileMode.OpenOrCreate, FileAccess.Write, FileShare.Read);
        file.Seek(offset, SeekOrigin.Begin);
        file.Write(payload.AsSpan(DumpHeaderFixedSize + nameLength));
        if (offset == 0) Log.Info($"Receiving crash dump {name} from {sender}");
    }

    private HashSet<string> DumpNames() =>
        Directory.Exists(_logDir) ? Directory.GetFiles(_logDir, DumpPattern).Select(Path.GetFileName).OfType<string>().ToHashSet() : [];

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
