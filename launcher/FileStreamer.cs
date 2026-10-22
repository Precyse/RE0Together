using System.Security.Cryptography;

namespace CoopLauncher;

/// <summary>The sending half of the FILE_BEGIN / CHUNK / END transfer, shared by the host's save sync (<see cref="SaveSender"/>) and
/// the guest's crash files (<see cref="CrashSender"/>): whole files held in memory, streamed paced, and resent until the other side
/// ACKs ok (ten tries, <see cref="RetryAfterMs"/> apart). Transfer ids are the sender's own; the ACK carries the id back.</summary>
public sealed class FileStreamer
{
    public const long RetryAfterMs = 5000;

    private const double BytesPerSecond = 4 * 1024 * 1024;
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

    private readonly ITransport _transport;
    private readonly Func<long> _clock;
    private readonly string _label;
    private readonly List<Outgoing> _pending = [];
    private uint _nextId = 1;
    private double _credit;
    private long _lastPumpMs;

    /// <param name="label">What the log lines call this transfer ("Save sync").</param>
    /// <param name="clock">Milliseconds; defaults to the system tick count, tests drive their own.</param>
    public FileStreamer(ITransport transport, string label, Func<long>? clock = null)
    {
        _transport = transport;
        _label = label;
        _clock = clock ?? (() => Environment.TickCount64);
        _lastPumpMs = _clock();
    }

    /// <summary>Queues a file for a peer, replacing a queued file of the same name.</summary>
    public void Enqueue(ulong peer, string name, byte[] data)
    {
        Remove(peer, name);
        _pending.Add(new Outgoing { Peer = peer, Name = name, Data = data, Hash = SHA256.HashData(data), Id = _nextId++ });
        Log.Info($"{_label}: sending {name} ({data.Length} bytes) to {peer}");
    }

    /// <summary>Drops everything still queued for a peer.</summary>
    public void RemovePeer(ulong peer) => _pending.RemoveAll(o => o.Peer == peer);

    public void Remove(ulong peer, string name) => _pending.RemoveAll(o => o.Peer == peer && o.Name == name);

    public void OnAck(ulong peer, Frame frame)
    {
        if (!FileMessages.TryParseAck(frame.Payload, out var id, out var ok)) return;
        var transfer = _pending.Find(o => o.Peer == peer && o.Id == id);
        if (transfer == null) return;
        if (ok)
        {
            Log.Info($"{_label}: {peer} confirmed {transfer.Name}");
            _pending.Remove(transfer);
        }
        else
        {
            Log.Info($"{_label}: {peer} rejected {transfer.Name}, resending in {RetryAfterMs / 1000} s");
            transfer.EndSentAtMs = _clock();
        }
    }

    public void Pump()
    {
        var now = _clock();
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
            Log.Info($"{_label}: giving up on {t.Name} for {t.Peer}");
            _pending.Remove(t);
            return;
        }
        t.Attempts++;
        t.Id = _nextId++;
        t.Offset = 0;
        t.BeginSent = false;
        t.EndSent = false;
    }
}
