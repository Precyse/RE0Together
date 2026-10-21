using System.Security.Cryptography;

namespace CoopLauncher;

/// <summary>Host side of save sync: streams the profile's files to a peer, paced, and resends a file until it is ACKed ok. A set the
/// guest could not take (too many names, a name that is not ASCII, a file over <see cref="SaveReceiver.MaxFileBytes"/>) is not sent
/// at all, and a missing save folder is an empty set, so the guest never waits for something that cannot arrive.</summary>
public sealed class SaveSender
{
    private const double BytesPerSecond = 4 * 1024 * 1024;
    private const long RetryAfterMs = 5000;
    private const int MaxAttempts = 10;
    private const double MaxBurstChunks = 2;

    private sealed class Outgoing
    {
        public required ulong Peer { get; init; }
        public required string Name { get; init; }
        public required byte[] Data { get; init; }
        public required byte[] Hash { get; init; }
        public uint Id { get; set; }
        public int Offset { get; set; }
        public bool BeginSent { get; set; }
        public bool EndSent { get; set; }
        public long EndSentAtMs { get; set; }
        public int Attempts { get; set; } = 1;
    }

    private readonly SaveSyncProfile _config;
    private readonly string _sourceDir;
    private readonly ITransport _transport;
    private readonly List<Outgoing> _pending = [];
    private readonly HashSet<string> _logged = [];
    private uint _nextId = 1;
    private double _credit;
    private long _lastPumpMs = Environment.TickCount64;

    public SaveSender(SaveSyncProfile config, string sourceDir, ITransport transport)
    {
        _config = config;
        _sourceDir = sourceDir;
        _transport = transport;
    }

    public void SendTo(ulong peer)
    {
        _pending.RemoveAll(o => o.Peer == peer);
        var names = FileNames();
        if (Unsendable(names) is { } reason)
        {
            LogOnce($"Save sync: not sending the save set to {peer}: {reason}");
            return;
        }
        if (_config.FilePattern != null)
        {
            _transport.Send(peer, FileMessages.Manifest(names));
            Log.Info($"Save sync: announced {names.Count} files to {peer}");
        }
        foreach (var name in names) Enqueue(peer, name);
    }

    /// <summary>Sends one file the host's game wrote after the join, with the manifest again (it may be a new name).
    /// False when the file could not be read (the game still holds it): ask again later.</summary>
    public bool SendChanged(ulong peer, string name)
    {
        _pending.RemoveAll(o => o.Peer == peer && o.Name == name);
        var names = FileNames();
        if (Unsendable(names) is { } reason)
        {
            LogOnce($"Save sync: not sending {name} to {peer}: {reason}");
            return true;
        }
        if (_config.FilePattern != null) _transport.Send(peer, FileMessages.Manifest(names));
        return Enqueue(peer, name);
    }

    /// <summary>Why the guest could not take this set, or null when it can.</summary>
    private string? Unsendable(IReadOnlyList<string> names)
    {
        if (names.Count > FileMessages.MaxManifestNames) return $"{names.Count} files, at most {FileMessages.MaxManifestNames} fit the manifest";
        if (names.FirstOrDefault(name => !FileMessages.IsWireName(name)) is { } badName) return $"'{badName}' is not an ASCII name of at most {byte.MaxValue} characters";
        var tooBig = names.FirstOrDefault(name => File.Exists(Path.Combine(_sourceDir, name)) && new FileInfo(Path.Combine(_sourceDir, name)).Length > SaveReceiver.MaxFileBytes);
        return tooBig == null ? null : $"{tooBig} is over {SaveReceiver.MaxFileBytes} bytes";
    }

    private void LogOnce(string message)
    {
        if (_logged.Add(message)) Log.Info(message);
    }

    private bool Enqueue(ulong peer, string name)
    {
        var path = Path.Combine(_sourceDir, name);
        if (!File.Exists(path))
        {
            Log.Info($"Save sync: {path} not found, nothing to send for {name}");
            return true;
        }
        byte[] data;
        try
        {
            data = File.ReadAllBytes(path);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            Log.Info($"Save sync: {path} cannot be read yet ({e.Message})");
            return false;
        }
        _pending.Add(new Outgoing { Peer = peer, Name = name, Data = data, Hash = SHA256.HashData(data), Id = _nextId++ });
        Log.Info($"Save sync: sending {name} ({data.Length} bytes) to {peer}");
        return true;
    }

    /// <summary>The profile's fixed list, or every file in the source folder that matches the profile's pattern (none when the folder is missing).</summary>
    private List<string> FileNames()
    {
        if (_config.FilePattern is not { } pattern) return _config.SteamRemoteFiles;
        try
        {
            return Directory.GetFiles(_sourceDir, pattern).Select(Path.GetFileName).OfType<string>().Order().ToList();
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            LogOnce($"Save sync: {_sourceDir} cannot be read ({e.Message}), sending an empty set");
            return [];
        }
    }

    public void OnAck(ulong peer, Frame frame)
    {
        if (!FileMessages.TryParseAck(frame.Payload, out var id, out var ok)) return;
        var transfer = _pending.Find(o => o.Peer == peer && o.Id == id);
        if (transfer == null) return;
        if (ok)
        {
            Log.Info($"Save sync: {peer} confirmed {transfer.Name}");
            _pending.Remove(transfer);
        }
        else
        {
            Log.Info($"Save sync: {peer} rejected {transfer.Name}, resending");
            Restart(transfer);
        }
    }

    public void Pump()
    {
        var now = Environment.TickCount64;
        var chunkBytes = _transport.MaxChunkBytes;
        _credit = Math.Min(_credit + BytesPerSecond * (now - _lastPumpMs) / 1000, chunkBytes * MaxBurstChunks);
        _lastPumpMs = now;

        foreach (var transfer in _pending.ToList())
        {
            if (transfer.EndSent)
            {
                if (now - transfer.EndSentAtMs >= RetryAfterMs) Retry(transfer);
                continue;
            }
            Stream(transfer, chunkBytes, now);
        }
    }

    private void Stream(Outgoing t, int chunkBytes, long now)
    {
        // A refused send (full send buffer) is retried on the next pump; offsets only advance on acceptance.
        if (!t.BeginSent)
        {
            if (!_transport.Send(t.Peer, FileMessages.Begin(new FileBegin(t.Id, (uint)t.Data.Length, t.Hash, t.Name)))) return;
            t.BeginSent = true;
        }
        while (_credit > 0 && t.Offset < t.Data.Length)
        {
            var length = Math.Min(chunkBytes, t.Data.Length - t.Offset);
            if (!_transport.Send(t.Peer, FileMessages.Chunk(t.Id, (uint)t.Offset, t.Data.AsSpan(t.Offset, length)))) return;
            t.Offset += length;
            _credit -= length;
        }
        if (t.Offset < t.Data.Length) return;
        if (!_transport.Send(t.Peer, FileMessages.End(t.Id))) return;
        t.EndSent = true;
        t.EndSentAtMs = now;
    }

    private void Retry(Outgoing t)
    {
        if (t.Attempts >= MaxAttempts)
        {
            Log.Info($"Save sync: giving up on {t.Name} for {t.Peer}");
            _pending.Remove(t);
            return;
        }
        Restart(t);
    }

    private void Restart(Outgoing t)
    {
        t.Attempts++;
        t.Id = _nextId++;
        t.Offset = 0;
        t.BeginSent = false;
        t.EndSent = false;
    }
}
