namespace CoopLauncher;

/// <summary>Host side of crash reporting: takes the files a guest's <see cref="CrashSender"/> sends (FILE_BEGIN / CHUNK / END), checks
/// size and sha256, writes them to <see cref="CrashRules.Destination"/> and ACKs. A guest gets <see cref="CrashRules.MaxIncidents"/>
/// folders per session; no consent is asked. One launcher log line per folder when its first file is saved. A file that cannot be
/// written fails only that transfer: the guest is told and resends.</summary>
public sealed class CrashReceiver
{
    private const string TempSuffix = ".part";

    private sealed class Incoming
    {
        public required FileBegin Begin { get; init; }
        public required string Folder { get; init; }
        public required string File { get; init; }
        public required string TempPath { get; init; }
        public required FileStream Stream { get; init; }
    }

    private readonly string _gameDir;
    private readonly ITransport _transport;
    private readonly Func<ulong, string> _nameOf;
    private readonly Dictionary<(ulong Peer, uint Id), Incoming> _incoming = [];
    private readonly Dictionary<ulong, HashSet<string>> _stamps = [];
    private readonly HashSet<string> _announced = [];

    internal CrashReceiver(string gameDir, ITransport transport, Func<ulong, string> nameOf)
    {
        _gameDir = gameDir;
        _transport = transport;
        _nameOf = nameOf;
    }

    /// <summary>Starts receiving on the host; a guest takes nothing.</summary>
    public static void Attach(string gameDir, Session session, ITransport transport, Func<ulong, string> nameOf)
    {
        if (session.IsHost) session.FileFrameReceived += new CrashReceiver(gameDir, transport, nameOf).OnFrame;
    }

    internal void OnFrame(ulong sender, Frame frame)
    {
        switch (frame.Type)
        {
            case Msg.FileBegin when FileMessages.TryParseBegin(frame.Payload, out var begin):
                Guarded(sender, begin.Id, () => OnBegin(sender, begin));
                break;
            case Msg.FileChunk when FileMessages.TryParseChunk(frame.Payload, out var chunk):
                Guarded(sender, chunk.Id, () => OnChunk(sender, chunk));
                break;
            case Msg.FileEnd when FileMessages.TryParseId(frame.Payload, out var id):
                Guarded(sender, id, () => OnEnd(sender, id));
                break;
        }
    }

    private void Guarded(ulong sender, uint id, Action handle)
    {
        try
        {
            handle();
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            Discard(sender, id);
            Log.Info($"Crash files: a file from {_nameOf(sender)} could not be written ({e.Message}), the guest will resend it");
            _transport.Send(sender, FileMessages.Ack(id, ok: false));
        }
    }

    private void OnBegin(ulong sender, FileBegin begin)
    {
        if (!CrashRules.TryParseWireName(begin.Name, out var stamp, out var file) || begin.Size > CrashRules.MaxFileBytes || !StampAllowed(sender, stamp))
        {
            Log.Info($"Crash files: ignoring '{begin.Name}' ({begin.Size} bytes) from {_nameOf(sender)}");
            return;
        }
        foreach (var stale in _incoming.Where(p => p.Key.Peer == sender && p.Value.Begin.Name == begin.Name).Select(p => p.Key.Id).ToList()) Discard(sender, stale);
        var folder = CrashRules.Destination(_gameDir, sender, stamp);
        Directory.CreateDirectory(folder);
        var tempPath = Path.Combine(folder, file + TempSuffix);
        _incoming[(sender, begin.Id)] = new Incoming
        {
            Begin = begin, Folder = folder, File = file, TempPath = tempPath, Stream = new FileStream(tempPath, FileMode.Create, FileAccess.Write),
        };
    }

    private bool StampAllowed(ulong sender, string stamp)
    {
        if (!_stamps.TryGetValue(sender, out var stamps)) _stamps[sender] = stamps = [];
        return stamps.Contains(stamp) || (stamps.Count < CrashRules.MaxIncidents && stamps.Add(stamp));
    }

    private void OnChunk(ulong sender, FileChunk chunk)
    {
        if (!_incoming.TryGetValue((sender, chunk.Id), out var incoming)) return;
        var stream = incoming.Stream;
        if (chunk.Offset != stream.Length || stream.Length + chunk.Data.Length > incoming.Begin.Size) return;
        stream.Write(chunk.Data.Span);
    }

    private void OnEnd(ulong sender, uint id)
    {
        if (!_incoming.TryGetValue((sender, id), out var incoming)) return;
        incoming.Stream.Dispose();
        var ok = new FileInfo(incoming.TempPath).Length == incoming.Begin.Size && FileMessages.HashMatches(incoming.TempPath, incoming.Begin.Hash);
        if (ok)
        {
            File.Move(incoming.TempPath, Path.Combine(incoming.Folder, incoming.File), overwrite: true);
            if (_announced.Add(incoming.Folder)) Log.Info($"Crash files from {_nameOf(sender)} saved to {incoming.Folder}");
        }
        else
        {
            File.Delete(incoming.TempPath);
            Log.Info($"Crash files: {incoming.File} from {_nameOf(sender)} failed verification");
        }
        _incoming.Remove((sender, id));
        _transport.Send(sender, FileMessages.Ack(id, ok));
    }

    private void Discard(ulong sender, uint id)
    {
        if (!_incoming.Remove((sender, id), out var incoming)) return;
        incoming.Stream.Dispose();
        try
        {
            File.Delete(incoming.TempPath);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            Log.Info($"Crash files: could not delete {incoming.TempPath} ({e.Message})");
        }
    }
}
