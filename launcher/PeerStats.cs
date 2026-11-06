using System.Diagnostics;

namespace CoopLauncher;

/// <summary>Per-peer ping scheduling, EWMA round-trip time and last answer, reported to the adapter and shown per player.</summary>
public sealed class PeerStats
{
    public const long SilentAfterMs = 5000;

    private const int PingIntervalMs = 1000;
    private const int ReportIntervalMs = 2000;
    private const double RttAlpha = 0.125;
    private const double MicrosPerMs = 1000.0;

    private readonly Func<long> _clock;
    private readonly Dictionary<byte, double> _rttMs = new();
    private readonly Dictionary<byte, long> _lastHeardMs = new();
    private readonly HashSet<byte> _heard = [];
    private readonly Dictionary<byte, ulong> _restartedMicros = new();
    private long _lastPing;
    private long _lastReport;

    /// <summary>The clock is milliseconds; tests drive their own.</summary>
    public PeerStats(Func<long>? clock = null) => _clock = clock ?? (() => Environment.TickCount64);

    public static ulong NowMicros => (ulong)(Stopwatch.GetTimestamp() * 1_000_000.0 / Stopwatch.Frequency);

    /// <summary>A pong answers the peer either way; its sample counts only when the ping was sent after the last
    /// <see cref="Restart"/>, so pongs queued behind a save transfer never seed the average.</summary>
    public void OnPong(byte slot, ulong echoedMicros)
    {
        OnHeard(slot);
        if (_restartedMicros.TryGetValue(slot, out var restarted) && echoedMicros < restarted) return;
        var sample = (NowMicros - echoedMicros) / MicrosPerMs;
        _rttMs[slot] = _rttMs.TryGetValue(slot, out var rtt) ? rtt + RttAlpha * (sample - rtt) : sample;
    }

    /// <summary>Any frame from the peer (a pong, a file acknowledgement, a game frame) shows it is there.</summary>
    public void OnHeard(byte slot)
    {
        _lastHeardMs[slot] = _clock();
        _heard.Add(slot);
    }

    /// <summary>Whether a frame has come from the peer since it was added; false while it is still being connected to.</summary>
    public bool HasHeard(byte slot) => _heard.Contains(slot);

    /// <summary>Forgets a peer's average: the next ping sent from now on starts it again.</summary>
    public void Restart(byte slot)
    {
        _rttMs.Remove(slot);
        _restartedMicros[slot] = NowMicros;
    }

    /// <summary>Round-trip time of the slowest peer, null before the first pong.</summary>
    public int? RttMs => _rttMs.Count == 0 ? null : (int)Math.Round(_rttMs.Values.Max());

    /// <summary>Round-trip time of one peer, null before its first pong.</summary>
    public int? RttOf(byte slot) => _rttMs.TryGetValue(slot, out var rtt) ? (int)Math.Round(rtt) : null;

    /// <summary>True when a peer that was heard from has been silent for <see cref="SilentAfterMs"/>; one never heard from is still
    /// connecting (<see cref="HasHeard"/>), not silent.</summary>
    public bool SilentFor(byte slot, long nowMs) => _heard.Contains(slot) && nowMs - _lastHeardMs[slot] > SilentAfterMs;

    public void Remove(byte slot)
    {
        _rttMs.Remove(slot);
        _lastHeardMs.Remove(slot);
        _heard.Remove(slot);
        _restartedMicros.Remove(slot);
    }

    public void Tick(IEnumerable<byte> peerSlots, Action<byte> sendPing, Action<byte, ushort> report)
    {
        var now = _clock();
        if (now - _lastPing >= PingIntervalMs)
        {
            _lastPing = now;
            foreach (var slot in peerSlots) sendPing(slot);
        }
        if (now - _lastReport >= ReportIntervalMs)
        {
            _lastReport = now;
            foreach (var (slot, rtt) in _rttMs) report(slot, (ushort)Math.Min(rtt, ushort.MaxValue));
        }
    }
}
