using CoopLauncher;

namespace LauncherTests;

/// <summary>FreshQueue: a newer unreliable frame of a fresh type replaces the unsent one of the same (peer, type); other
/// types, other peers and reliable frames are untouched; profile parsing refuses non-game types.</summary>
public static class FreshQueueTests
{
    private const ushort Pose = 0x0140;
    private const ushort EnemyPose = 0x0160;
    private const ushort Plain = 0x0100;
    private const ulong PeerA = 7;
    private const ulong PeerB = 8;
    private const byte Reliable = Framing.FlagReliable;
    private const byte Unreliable = 0;

    private static Frame FrameOf(ushort type, byte flags, byte marker) => new(type, flags, 0, [marker]);

    private static FreshQueue QueueOf() => new([Pose, EnemyPose]);

    public static void Run()
    {
        NewestReplaces();
        OthersUntouched();
        ReliableNeverHeld();
        DrainOrder();
        Parsing();
    }

    private static void NewestReplaces()
    {
        var queue = QueueOf();
        queue.Enqueue(PeerA, FrameOf(Pose, Unreliable, 1));
        queue.Enqueue(PeerA, FrameOf(Pose, Unreliable, 2));
        queue.Enqueue(PeerA, FrameOf(Pose, Unreliable, 3));
        var drained = queue.Drain();
        Check.That(drained.Count == 1 && drained[0].Frame.Payload[0] == 3, "three poses queued: only the newest is sent");
        Check.That(queue.Replaced == 2, "two older frames counted as replaced");
        Check.That(queue.Drain().Count == 0, "a drained queue is empty");
        queue.Enqueue(PeerA, FrameOf(Pose, Unreliable, 4));
        Check.That(queue.Drain()[0].Frame.Payload[0] == 4 && queue.Replaced == 2, "after a flush the next frame is held fresh, not replaced");
    }

    private static void OthersUntouched()
    {
        var queue = QueueOf();
        queue.Enqueue(PeerA, FrameOf(Pose, Unreliable, 1));
        queue.Enqueue(PeerA, FrameOf(EnemyPose, Unreliable, 2));
        queue.Enqueue(PeerB, FrameOf(Pose, Unreliable, 3));
        var drained = queue.Drain();
        Check.That(drained.Count == 3 && queue.Replaced == 0, "another type and another peer each keep their own frame");
        Check.That(!queue.Accepts(FrameOf(Plain, Unreliable, 0)), "a type outside the list is not held");
    }

    private static void ReliableNeverHeld()
    {
        var queue = QueueOf();
        Check.That(!queue.Accepts(FrameOf(Pose, Reliable, 0)), "a reliable frame of a fresh type is not held");
        Check.That(queue.Accepts(FrameOf(Pose, Unreliable, 0)), "an unreliable frame of a fresh type is held");
    }

    private static void DrainOrder()
    {
        var queue = QueueOf();
        queue.Enqueue(PeerB, FrameOf(EnemyPose, Unreliable, 1));
        queue.Enqueue(PeerA, FrameOf(Pose, Unreliable, 2));
        queue.Enqueue(PeerB, FrameOf(EnemyPose, Unreliable, 3));
        var drained = queue.Drain();
        Check.That(drained.Count == 2 && drained[0].Peer == PeerB && drained[0].Frame.Payload[0] == 3 && drained[1].Peer == PeerA,
            "frames leave in first-queued order, a replacement keeps its place");
    }

    private static void Parsing()
    {
        Check.That(FreshQueue.ParseTypes(["0x0140", "0x0160", "0x0173"]).SequenceEqual([(ushort)0x0140, (ushort)0x0160, (ushort)0x0173]), "hex names parse");
        Check.That(FreshQueue.ParseTypes(null).Count == 0, "no entry: no types");
        Check.That(Throws(["0x00ff"]), "a launcher-owned type is refused");
        Check.That(Throws(["pose"]), "a non-hex name is refused");
        Check.That(Throws(["0x10000"]), "a type above 16 bits is refused");

        var plain = new GameProfile("g", "G", 1, "g.exe", 2, 1, []);
        Check.That(FreshQueue.ForProfile(plain) == null, "a profile without freshTypes gets no queue (sends as before)");
        var fresh = FreshQueue.ForProfile(plain with { FreshTypes = ["0x0140"] });
        Check.That(fresh is { TypeCount: 1 }, "a profile with freshTypes gets a queue");
    }

    private static bool Throws(string[] names)
    {
        try
        {
            FreshQueue.ParseTypes(names);
            return false;
        }
        catch (InvalidDataException)
        {
            return true;
        }
    }
}
