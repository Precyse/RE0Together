using CoopLauncher;

namespace LauncherTests;

/// <summary>PeerStats: per-slot round-trip time and silence, on a clock the test drives.</summary>
public static class PeerStatsTests
{
    private const byte Slot1 = 1;
    private const byte Slot2 = 2;
    private const long Start = 100_000;

    public static void Run()
    {
        var now = Start;
        var stats = new PeerStats(() => now);
        var pings = new List<byte>();
        void Tick() => stats.Tick([Slot1, Slot2], pings.Add, (_, _) => { });

        Check.That(stats.RttOf(Slot1) == null && stats.RttMs == null, "no round-trip time before the first pong");
        Check.That(!stats.SilentFor(Slot1, now), "a slot that was never pinged is not silent");

        Tick();
        Check.That(pings.SequenceEqual([Slot1, Slot2]), "both slots are pinged");
        Check.That(!stats.SilentFor(Slot1, now + PeerStats.SilentAfterMs), "a slot that never answered is silent only after the limit");
        Check.That(stats.SilentFor(Slot1, now + PeerStats.SilentAfterMs + 1), "a slot that never answered counts from its first ping");

        now += 2000;
        stats.OnPong(Slot1, PeerStats.NowMicros - 40_000);
        Check.That(stats.RttOf(Slot1) is >= 40 and < 100 && stats.RttOf(Slot2) == null, "a pong gives that slot its round-trip time");
        Check.That(!stats.SilentFor(Slot1, now + PeerStats.SilentAfterMs), "an answer restarts the silence");
        Check.That(stats.SilentFor(Slot1, now + PeerStats.SilentAfterMs + 1), "silent again after the limit");

        stats.OnPong(Slot2, PeerStats.NowMicros - 200_000);
        Check.That(stats.RttMs == stats.RttOf(Slot2) && stats.RttMs > stats.RttOf(Slot1), "the top bar value is the slowest slot");

        stats.Remove(Slot2);
        Check.That(stats.RttOf(Slot2) == null && stats.RttMs == stats.RttOf(Slot1), "Remove forgets the round-trip time");
        Check.That(!stats.SilentFor(Slot2, now + 1_000_000), "Remove forgets the silence too");
    }
}
