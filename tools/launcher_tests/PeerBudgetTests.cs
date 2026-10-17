using CoopLauncher;

namespace LauncherTests;

/// <summary>TokenBucket (integer math) and PeerBudget (four buckets per slot, drops counted per slot, type and reliability).</summary>
public static class PeerBudgetTests
{
    private const ushort GameType = 0x0100;
    private const ushort OtherGameType = 0x0101;
    private const byte Reliable = Framing.FlagReliable;
    private const byte Unreliable = 0;
    private const int UnreliableFrameBurst = 600;
    private const int UnreliableFramesPerSecond = 300;
    private const int ReliableFrameBurst = 3000;
    private const int KiB = 1024;

    private static Frame FrameOf(ushort type, byte flags, int bytes = 0) => new(type, flags, 0, new byte[bytes]);

    private static int Admitted(PeerBudget budget, byte slot, int count, long nowMs, Frame frame) =>
        Enumerable.Range(0, count).Count(_ => budget.Admit(slot, frame, nowMs));

    public static void Run()
    {
        Bucket();
        Frames();
        Bytes();
        Report();
    }

    private static void Bucket()
    {
        var bucket = new TokenBucket(3, 10, 0);
        Check.That(bucket.Tokens == 10, "starts full");
        bucket.Take(10);
        bucket.Refill(1000);
        Check.That(bucket.Tokens == 3, "a second at 3/s: 3 tokens");
        var carry = new TokenBucket(3, 10, 0);
        carry.Take(10);
        carry.Refill(334);
        carry.Refill(667);
        carry.Refill(1000);
        Check.That(carry.Tokens == 3, "refills in small steps lose nothing to rounding (remainder carried)");
        bucket.Refill(100_000);
        Check.That(bucket.Tokens == 10, "never above the capacity");
        Check.That(bucket.Has(10) && !bucket.Has(11), "Has compares with the tokens");

        var capped = new TokenBucket(1000, 1_000_000_000, 0);
        capped.Take(1_000_000_000);
        capped.Refill(120_000);
        Check.That(capped.Tokens == 60_000, "elapsed time is capped at 60 s");
        capped.Refill(100_000);
        Check.That(capped.Tokens == 60_000, "a clock that went back adds nothing");
    }

    private static void Frames()
    {
        var budget = new PeerBudget();
        var frame = FrameOf(GameType, Unreliable);
        Check.That(Admitted(budget, 1, UnreliableFrameBurst + 50, 0, frame) == UnreliableFrameBurst, "unreliable burst: 600 frames, the rest dropped");
        Check.That(Admitted(budget, 1, 1000, 1000, frame) == UnreliableFramesPerSecond, "one second later: 300 more");
        Check.That(Admitted(budget, 2, 10, 1000, frame) == 10, "another slot has its own budget");
        Check.That(Admitted(budget, 1, ReliableFrameBurst + 1, 1000, FrameOf(GameType, Reliable)) == ReliableFrameBurst,
            "reliable frames have their own buckets: 3000 in a burst");
        Check.That(budget.Admit(1, FrameOf(Msg.BuildInfo, Unreliable, 100), 1000) && budget.Admit(1, FrameOf(Msg.Ping, Unreliable), 1000),
            "launcher-owned frames are not budgeted");
        budget.Remove(1);
        Check.That(Admitted(budget, 1, UnreliableFrameBurst, 1000, frame) == UnreliableFrameBurst, "a removed slot starts with fresh buckets");
    }

    private static void Bytes()
    {
        var budget = new PeerBudget();
        var big = FrameOf(GameType, Unreliable, 100 * KiB);
        Check.That(Admitted(budget, 1, 8, 0, big) == 5, "512 KiB of unreliable bytes: five 100 KiB frames");
        Check.That(Admitted(budget, 1, 1, 300, big) == 0, "bytes refill at 256 KiB/s: 300 ms is not enough");
        Check.That(Admitted(budget, 1, 1, 1000, big) == 1, "a second later one more fits");

        var frameFirst = new PeerBudget();
        Admitted(frameFirst, 1, 5, 0, big);
        Admitted(frameFirst, 1, 1, 0, big);
        Check.That(Admitted(frameFirst, 1, UnreliableFrameBurst - 5, 0, FrameOf(GameType, Unreliable, 1)) == UnreliableFrameBurst - 5,
            "a frame dropped for its bytes does not use up a frame token");
    }

    private static void Report()
    {
        var budget = new PeerBudget();
        Check.That(budget.TakeReport().Count == 0, "no drops: no lines");
        Admitted(budget, 1, UnreliableFrameBurst + 12, 0, FrameOf(OtherGameType, Unreliable));
        var first = budget.TakeReport();
        Check.That(first.Count == 1 && first[0] == "Peer budget: slot 1 dropped 0x0101 x12 (unreliable)", "one line per slot, type and reliability");
        Check.That(budget.TakeReport().Count == 0, "no change: no line");
        Admitted(budget, 1, 3, 0, FrameOf(OtherGameType, Unreliable));
        Check.That(budget.TakeReport()[0].Contains("x15"), "totals are cumulative");
        Admitted(budget, 1, ReliableFrameBurst + 4, 0, FrameOf(GameType, Reliable));
        var reliable = budget.TakeReport();
        Check.That(reliable.Count == 1 && reliable[0].Contains("(reliable)") && reliable[0].Contains("state may diverge; F9 resyncs"),
            "a reliable drop says the state may diverge");
    }
}
