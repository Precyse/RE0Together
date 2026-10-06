using System.Security.Cryptography;

namespace CoopLauncher;

/// <summary>Guest side of save sync: writes incoming files to a temp file, verifies sha256, moves them into the session dir and ACKs.
/// Once the first full set has arrived, later files (the host saving mid-session) go to a staging folder instead, and
/// <see cref="PromoteStaged"/> moves them into the session dir when the game is not running, so the guest's own saves
/// are never replaced under a running game.</summary>
public sealed class SaveReceiver
{
    private const long MaxFileBytes = 64L * 1024 * 1024;
    public const string StagingFolder = "staging";  // under the session dir: files the host sent after the first full set
    private const string TempSuffix = ".part";

    private sealed class Incoming
    {
        public required FileBegin Begin { get; init; }
        public required string TempPath { get; init; }
        public required FileStream Stream { get; init; }
        public required string FinalDir { get; init; }
    }

    private readonly SaveSyncProfile _config;
    private readonly string _sessionDir;
    private readonly string _stagingDir;
    private readonly ITransport _transport;
    private readonly Func<ulong> _hostId;
    private readonly Dictionary<uint, Incoming> _incoming = new();
    private readonly HashSet<string> _received = [];
    private List<string>? _expected;  // the host's manifest when the profile sends by pattern
    private bool _initialSetDone;

    public SaveReceiver(SaveSyncProfile config, string sessionDir, string stagingDir, ITransport transport, Func<ulong> hostId)
    {
        _config = config;
        _sessionDir = sessionDir;
        _stagingDir = stagingDir;
        _transport = transport;
        _hostId = hostId;
        if (config.FilePattern == null) _expected = config.SteamRemoteFiles;
        UpdateComplete();
    }

    /// <summary>True once every expected file (the profile's list, or the host's manifest) has arrived and verified. Stays
    /// true: files the host sends later are staged, they do not reopen the first sync.</summary>
    public bool Complete => _initialSetDone;

    public void OnFrame(ulong sender, Frame frame)
    {
        if (sender != _hostId()) return;
        switch (frame.Type)
        {
            case Msg.FileManifest when _config.FilePattern != null && FileMessages.TryParseManifest(frame.Payload, out var names):
                _expected = names;
                _received.RemoveWhere(name => !names.Contains(name));
                Log.Info($"Save sync: host announced {names.Count} files");
                UpdateComplete();
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

    /// <summary>Moves the staged files into the session dir, replacing the guest's own copies. Call only while the game is not running.</summary>
    public void PromoteStaged()
    {
        if (!Directory.Exists(_stagingDir)) return;
        foreach (var path in Directory.GetFiles(_stagingDir).Where(p => !p.EndsWith(TempSuffix)))
        {
            try
            {
                Directory.CreateDirectory(_sessionDir);
                File.Move(path, Path.Combine(_sessionDir, Path.GetFileName(path)), overwrite: true);
                Log.Info($"Save sync: moved staged {Path.GetFileName(path)} into the session");
            }
            catch (IOException e)
            {
                Log.Info($"Save sync: staged {Path.GetFileName(path)} not moved yet ({e.Message})");
            }
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
        var finalDir = _initialSetDone ? _stagingDir : _sessionDir;
        Directory.CreateDirectory(finalDir);
        var tempPath = Path.Combine(finalDir, begin.Name + TempSuffix);
        _incoming[begin.Id] = new Incoming
        {
            Begin = begin, TempPath = tempPath, FinalDir = finalDir, Stream = new FileStream(tempPath, FileMode.Create, FileAccess.Write),
        };
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
            File.Move(incoming.TempPath, Path.Combine(incoming.FinalDir, begin.Name), overwrite: true);
            _received.Add(begin.Name);
            _incoming.Remove(id);
            Log.Info($"Save sync: received {begin.Name} ({begin.Size} bytes)" + (incoming.FinalDir == _stagingDir ? " into staging" : ""));
            UpdateComplete();
        }
        else
        {
            File.Delete(incoming.TempPath);
            _incoming.Remove(id);
            Log.Info($"Save sync: {begin.Name} failed verification");
        }
        _transport.Send(sender, FileMessages.Ack(id, ok));
    }

    private void UpdateComplete()
    {
        if (_expected != null && _expected.All(_received.Contains)) _initialSetDone = true;
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
