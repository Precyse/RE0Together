namespace CoopLauncher;

/// <summary>Integer token bucket: refills <c>ratePerSecond</c> tokens a second up to <c>capacity</c>, starting full. Pure: the caller supplies the clock.</summary>
public sealed class TokenBucket
{
    private const long MsPerSecond = 1000;
    private const long MaxElapsedMs = 60_000;

    private readonly long _ratePerSecond;
    private readonly long _capacity;
    private long _tokens;
    private long _lastMs;
    private long _remainder;  // refill below one token, in token-milliseconds

    public TokenBucket(long ratePerSecond, long capacity, long nowMs)
    {
        _ratePerSecond = ratePerSecond;
        _capacity = capacity;
        _tokens = capacity;
        _lastMs = nowMs;
    }

    public long Tokens => _tokens;

    public void Refill(long nowMs)
    {
        var elapsed = Math.Clamp(nowMs - _lastMs, 0, MaxElapsedMs);
        _lastMs = nowMs;
        var units = elapsed * _ratePerSecond + _remainder;
        _tokens = Math.Min(_capacity, _tokens + units / MsPerSecond);
        _remainder = _tokens == _capacity ? 0 : units % MsPerSecond;
    }

    public bool Has(long cost) => _tokens >= cost;

    public void Take(long cost) => _tokens -= cost;
}

/// <summary>
/// A cap on what one peer can push into the game through its launcher: per source slot, game frames (type 0x0100 and up)
/// must fit a frame bucket and a byte bucket, one pair for unreliable and one for reliable frames. A frame that does not
/// fit is dropped and counted per (slot, type, reliability); the peer is never disconnected. Launcher-owned frames are
/// not budgeted.
/// </summary>
public sealed class PeerBudget
{
    private const long UnreliableFramesPerSecond = 300;
    private const long UnreliableFrameCapacity = 600;
    private const long UnreliableBytesPerSecond = 256 * 1024;
    private const long UnreliableByteCapacity = 512 * 1024;
    private const long ReliableFramesPerSecond = 300;
    private const long ReliableFrameCapacity = 3000;
    private const long ReliableBytesPerSecond = 512 * 1024;
    private const long ReliableByteCapacity = 4 * 1024 * 1024;

    private sealed record Buckets(TokenBucket Frames, TokenBucket Bytes);

    private readonly record struct DropKey(byte Slot, ushort Type, bool Reliable);

    private readonly Dictionary<(byte Slot, bool Reliable), Buckets> _buckets = new();
    private readonly Dictionary<DropKey, long> _dropped = new();
    private readonly Dictionary<DropKey, long> _reported = new();

    /// <summary>True when the frame may go on to the game; false when it is over budget (counted).</summary>
    public bool Admit(byte slot, Frame frame, long nowMs)
    {
        if (frame.Type < Msg.FirstGameType) return true;
        var reliable = (frame.Flags & Framing.FlagReliable) != 0;
        var buckets = BucketsFor(slot, reliable, nowMs);
        buckets.Frames.Refill(nowMs);
        buckets.Bytes.Refill(nowMs);
        var bytes = frame.Payload.Length;
        if (buckets.Frames.Has(1) && buckets.Bytes.Has(bytes))
        {
            buckets.Frames.Take(1);
            buckets.Bytes.Take(bytes);
            return true;
        }
        var key = new DropKey(slot, frame.Type, reliable);
        _dropped[key] = _dropped.GetValueOrDefault(key) + 1;
        return false;
    }

    /// <summary>The peer in this slot is gone: its buckets are forgotten (the drop counts stay for the next report).</summary>
    public void Remove(byte slot)
    {
        _buckets.Remove((slot, false));
        _buckets.Remove((slot, true));
    }

    /// <summary>One log line per (slot, type, reliability) whose drop total changed since the last call.</summary>
    public IReadOnlyList<string> TakeReport()
    {
        var lines = new List<string>();
        foreach (var (key, total) in _dropped.OrderBy(entry => entry.Key.Slot).ThenBy(entry => entry.Key.Type).ThenBy(entry => entry.Key.Reliable))
        {
            if (_reported.GetValueOrDefault(key) == total) continue;
            _reported[key] = total;
            var line = $"Peer budget: slot {key.Slot} dropped 0x{key.Type:x4} x{total} ({(key.Reliable ? "reliable" : "unreliable")})";
            lines.Add(key.Reliable ? line + ", state may diverge; F9 resyncs" : line);
        }
        return lines;
    }

    private Buckets BucketsFor(byte slot, bool reliable, long nowMs)
    {
        if (_buckets.TryGetValue((slot, reliable), out var existing)) return existing;
        var created = reliable
            ? new Buckets(new TokenBucket(ReliableFramesPerSecond, ReliableFrameCapacity, nowMs),
                new TokenBucket(ReliableBytesPerSecond, ReliableByteCapacity, nowMs))
            : new Buckets(new TokenBucket(UnreliableFramesPerSecond, UnreliableFrameCapacity, nowMs),
                new TokenBucket(UnreliableBytesPerSecond, UnreliableByteCapacity, nowMs));
        _buckets[(slot, reliable)] = created;
        return created;
    }
}
