using System.Diagnostics;

namespace CoopLauncher;

/// <summary>Per-peer ping scheduling and EWMA round-trip time, reported to the adapter.</summary>
public sealed class PeerStats
{
    private const int PingIntervalMs = 1000;
    private const int ReportIntervalMs = 2000;
    private const double RttAlpha = 0.125;
    private const double MicrosPerMs = 1000.0;

    private readonly Dictionary<byte, double> _rttMs = new();
    private long _lastPing;
    private long _lastReport;

    public static ulong NowMicros => (ulong)(Stopwatch.GetTimestamp() * 1_000_000.0 / Stopwatch.Frequency);

    public void OnPong(byte slot, ulong echoedMicros)
    {
        var sample = (NowMicros - echoedMicros) / MicrosPerMs;
        _rttMs[slot] = _rttMs.TryGetValue(slot, out var rtt) ? rtt + RttAlpha * (sample - rtt) : sample;
    }

    /// <summary>Round-trip time of the slowest peer, null before the first pong.</summary>
    public int? RttMs => _rttMs.Count == 0 ? null : (int)Math.Round(_rttMs.Values.Max());

    public void Remove(byte slot) => _rttMs.Remove(slot);

    public void Tick(IEnumerable<byte> peerSlots, Action<byte> sendPing, Action<byte, ushort> report)
    {
        var now = Environment.TickCount64;
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
