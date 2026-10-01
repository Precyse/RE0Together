using System.Buffers.Binary;
using System.Text;

namespace CoopLauncher;

/// <summary>
/// Guests send their diagnostics to the host: the lines their adapter appends to its log (LOG_APPEND) and any
/// crash dump the adapter writes during the session (CRASH_DUMP). The host appends log lines to one file per peer
/// and writes each dump as peer_&lt;id&gt;_&lt;name&gt;, both next to its own adapter log.
/// </summary>
public sealed class LogForwarder
{
    private const long SendIntervalMs = 2000;
    private const int MaxBytesPerSend = 32 * 1024;
    private const string PeerPrefix = "peer_";
    private const string DumpPattern = "crash-*.dmp";
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
    public static LogForwarder? Create(GameProfile profile, string gameDir, Session session, ITransport transport, ILobby lobby)
    {
        if (profile.AdapterLog is not { } log) return null;
        var forwarder = new LogForwarder(Path.Combine(gameDir, log), transport, () => lobby.OwnerId, lobby.OwnerId == transport.LocalId);
        session.DiagnosticsFrameReceived += forwarder.OnFrame;
        return forwarder;
    }

    public void Pump()
    {
        if (_isHost) return;
        SendDumpChunk();
        var now = Environment.TickCount64;
        if (now - _lastSendMs < SendIntervalMs) return;
        _lastSendMs = now;
        StartNewDump();
        SendLogLines();
    }

    private void SendLogLines()
    {
        var length = LogLength();
        if (length < _offset) _offset = 0;
        if (length == _offset) return;
        var chunk = ReadFrom(_offset, (int)Math.Min(MaxBytesPerSend, length - _offset));
        if (chunk.Length > 0 && _transport.Send(_hostId(), new Frame(Msg.LogAppend, Framing.FlagReliable, 0, chunk)))
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
        if (!_transport.Send(_hostId(), new Frame(Msg.CrashDump, Framing.FlagReliable, 0, payload))) return;
        _dump.Offset += length;
        if (_dump.Offset >= _dump.Data.Length) _dump = null;
    }

    private void OnFrame(ulong sender, Frame frame)
    {
        if (!_isHost || frame.Payload.Length == 0) return;
        if (frame.Type == Msg.LogAppend)
        {
            using var file = new FileStream(Path.Combine(_logDir, $"{PeerPrefix}{sender}.log"), FileMode.Append, FileAccess.Write, FileShare.ReadWrite);
            file.Write(frame.Payload);
            return;
        }
        WriteDumpChunk(sender, frame.Payload);
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
