using System.Security.Cryptography;

namespace CoopLauncher;

/// <summary>Guest side of save sync: writes incoming files to a temp file, verifies sha256, moves them into the session dir and ACKs.</summary>
public sealed class SaveReceiver
{
    private const long MaxFileBytes = 64L * 1024 * 1024;
    private const string TempSuffix = ".part";

    private sealed class Incoming
    {
        public required FileBegin Begin { get; init; }
        public required string TempPath { get; init; }
        public required FileStream Stream { get; init; }
    }

    private readonly SaveSyncProfile _config;
    private readonly string _sessionDir;
    private readonly ITransport _transport;
    private readonly Func<ulong> _hostId;
    private readonly Dictionary<uint, Incoming> _incoming = new();
    private readonly HashSet<string> _received = [];
    private List<string>? _expected;  // the host's manifest when the profile sends by pattern

    public SaveReceiver(SaveSyncProfile config, string sessionDir, ITransport transport, Func<ulong> hostId)
    {
        _config = config;
        _sessionDir = sessionDir;
        _transport = transport;
        _hostId = hostId;
        if (config.FilePattern == null) _expected = config.SteamRemoteFiles;
    }

    /// <summary>True once every expected file (the profile's list, or the host's manifest) has arrived and verified.</summary>
    public bool Complete => _expected != null && _expected.All(_received.Contains);

    public void OnFrame(ulong sender, Frame frame)
    {
        if (sender != _hostId()) return;
        switch (frame.Type)
        {
            case Msg.FileManifest when _config.FilePattern != null && FileMessages.TryParseManifest(frame.Payload, out var names):
                _expected = names;
                _received.RemoveWhere(name => !names.Contains(name));
                Log.Info($"Save sync: host announced {names.Count} files");
                break;
            case Msg.FileBegin when FileMessages.TryParseBegin(frame.Payload, out var begin):
                OnBegin(begin);
                break;
            case Msg.FileChunk when FileMessages.TryParseChunk(frame.Payload, out var chunk):
                OnChunk(chunk);
                break;
            case Msg.FileEnd when FileMessages.TryParseId(frame.Payload, out var id):
                OnEnd(sender, id);
                break;
        }
    }

    public void Abort()
    {
        foreach (var id in _incoming.Keys.ToList()) Discard(id);
    }

    private void OnBegin(FileBegin begin)
    {
        if (_expected == null || !_expected.Contains(begin.Name) || Path.GetFileName(begin.Name) != begin.Name || begin.Size > MaxFileBytes)
        {
            Log.Info($"Save sync: ignoring unexpected file '{begin.Name}' ({begin.Size} bytes)");
            return;
        }
        foreach (var stale in _incoming.Where(p => p.Value.Begin.Name == begin.Name).Select(p => p.Key).ToList()) Discard(stale);
        Directory.CreateDirectory(_sessionDir);
        var tempPath = Path.Combine(_sessionDir, begin.Name + TempSuffix);
        _incoming[begin.Id] = new Incoming { Begin = begin, TempPath = tempPath, Stream = new FileStream(tempPath, FileMode.Create, FileAccess.Write) };
    }

    private void OnChunk(FileChunk chunk)
    {
        if (!_incoming.TryGetValue(chunk.Id, out var incoming)) return;
        var stream = incoming.Stream;
        if (chunk.Offset != stream.Length || stream.Length + chunk.Data.Length > incoming.Begin.Size) return;
        stream.Write(chunk.Data.Span);
    }

    private void OnEnd(ulong sender, uint id)
    {
        if (!_incoming.TryGetValue(id, out var incoming)) return;
        incoming.Stream.Dispose();
        var begin = incoming.Begin;
        var ok = new FileInfo(incoming.TempPath).Length == begin.Size && HashMatches(incoming.TempPath, begin.Hash);
        if (ok)
        {
            File.Move(incoming.TempPath, Path.Combine(_sessionDir, begin.Name), overwrite: true);
            _received.Add(begin.Name);
            _incoming.Remove(id);
            Log.Info($"Save sync: received {begin.Name} ({begin.Size} bytes)");
        }
        else
        {
            File.Delete(incoming.TempPath);
            _incoming.Remove(id);
            Log.Info($"Save sync: {begin.Name} failed verification");
        }
        _transport.Send(sender, FileMessages.Ack(id, ok));
    }

    private void Discard(uint id)
    {
        var incoming = _incoming[id];
        incoming.Stream.Dispose();
        File.Delete(incoming.TempPath);
        _incoming.Remove(id);
    }

    private static bool HashMatches(string path, byte[] expected)
    {
        using var stream = File.OpenRead(path);
        return SHA256.HashData(stream).AsSpan().SequenceEqual(expected);
    }
}
