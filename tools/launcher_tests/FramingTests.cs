using CoopLauncher;

namespace LauncherTests;

/// <summary>The control and file message builders all go through Framing.Reliable: every builder's wire bytes equal the bytes the
/// two private Make helpers produced before (golden hex taken from that code).</summary>
public static class FramingTests
{
    private static readonly byte[] Hash = Enumerable.Range(0, FileMessages.HashSize).Select(i => (byte)i).ToArray();

    private static string Wire(Frame frame) => Convert.ToHexString(Framing.EncodeWire(frame));

    public static void Run()
    {
        Same("Welcome", ControlMessages.Welcome(1, 0, 4, 7, 0x0102030405060708), "02000100010004070000000807060504030201");
        Same("PeerUp", ControlMessages.PeerUp(2, 0x1112131415161718, "Ann"), "0300010002181716151413121103416E6E");
        Same("PeerDown", ControlMessages.PeerDown(3), "0400010003");
        Same("Reject", ControlMessages.Reject("nope"), "05000100046E6F7065");
        Same("Ping", ControlMessages.Ping(123456789), "1000000015CD5B0700000000");
        Same("Pong", ControlMessages.Pong(987654321), "11000000B168DE3A00000000");
        Same("PeerStats", ControlMessages.PeerStats(1, 0x1234), "12000100013412");
        Same("Heartbeat", ControlMessages.Heartbeat(), "20000100");
        Same("FileBegin", FileMessages.Begin(new FileBegin(5, 1000, Hash, "slot1.sav")),
            "4000010005000000E8030000000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F09736C6F74312E736176");
        Same("FileChunk", FileMessages.Chunk(5, 64, [9, 8, 7]), "410001000500000040000000090807");
        Same("FileEnd", FileMessages.End(5), "4200010005000000");
        Same("FileAck ok", FileMessages.Ack(5, true), "430001000500000001");
        Same("FileAck failed", FileMessages.Ack(5, false), "430001000500000000");
        Same("FileManifest", FileMessages.Manifest(["a.dat", "bb.dat"]), "440001000205612E6461740662622E646174");

        var reliable = Framing.Reliable(Msg.Heartbeat, []);
        Check.That(reliable.Flags == Framing.FlagReliable && reliable.Slot == Framing.NoSlot, "Framing.Reliable: reliable flag, no slot");
        Check.That(FileMessages.IsWireName("slot1.sav") && !FileMessages.IsWireName("später.sav") && !FileMessages.IsWireName(new string('a', 256)),
            "wire names: ASCII, at most 255 bytes");
    }

    private static void Same(string what, Frame frame, string goldenHex) => Check.That(Wire(frame) == goldenHex, $"{what} wire bytes unchanged");
}
