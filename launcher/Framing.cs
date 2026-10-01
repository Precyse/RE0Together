using System.Buffers.Binary;

namespace CoopLauncher;

public readonly record struct Frame(ushort Type, byte Flags, byte Slot, byte[] Payload);

public static class Msg
{
    public const ushort Hello = 0x0001;
    public const ushort Welcome = 0x0002;
    public const ushort PeerUp = 0x0003;
    public const ushort PeerDown = 0x0004;
    public const ushort Reject = 0x0005;
    public const ushort Ping = 0x0010;
    public const ushort Pong = 0x0011;
    public const ushort PeerStats = 0x0012;
    public const ushort BuildInfo = 0x0013;
    public const ushort Heartbeat = 0x0020;
    public const ushort FileBegin = 0x0040;
    public const ushort FileChunk = 0x0041;
    public const ushort FileEnd = 0x0042;
    public const ushort FileAck = 0x0043;
    public const ushort LogAppend = 0x0050;
    public const ushort CrashDump = 0x0051;
    public const ushort SaveChanged = 0x0060;
    public const ushort FirstGameType = 0x0100;

    public static bool IsFileTransfer(ushort type) => type is >= FileBegin and <= FileAck;
}

public static class Framing
{
    public const ushort ProtocolVersion = 1;
    public const byte FlagReliable = 1;
    public const byte SlotAll = 0xFF;
    public const int MaxFrameLen = 1 << 20;

    private const int LenFieldSize = 4;
    private const int WireHeaderSize = 4;

    public static byte[] EncodeWire(Frame f)
    {
        var buf = new byte[WireHeaderSize + f.Payload.Length];
        BinaryPrimitives.WriteUInt16LittleEndian(buf, f.Type);
        buf[2] = f.Flags;
        buf[3] = f.Slot;
        f.Payload.CopyTo(buf, WireHeaderSize);
        return buf;
    }

    public static byte[] EncodeLoopback(Frame f)
    {
        var buf = new byte[LenFieldSize + WireHeaderSize + f.Payload.Length];
        BinaryPrimitives.WriteUInt32LittleEndian(buf, (uint)(WireHeaderSize + f.Payload.Length));
        EncodeWire(f).CopyTo(buf, LenFieldSize);
        return buf;
    }

    public static bool TryDecodeWire(ReadOnlySpan<byte> data, out Frame frame)
    {
        if (data.Length < WireHeaderSize)
        {
            frame = default;
            return false;
        }
        frame = new Frame(BinaryPrimitives.ReadUInt16LittleEndian(data), data[2], data[3],
            data[WireHeaderSize..].ToArray());
        return true;
    }

    /// <summary>Blocking read of one loopback frame. False on EOF or an invalid length.</summary>
    public static bool TryReadLoopback(Stream stream, out Frame frame)
    {
        frame = default;
        try
        {
            var lenBuf = new byte[LenFieldSize];
            stream.ReadExactly(lenBuf);
            var len = BinaryPrimitives.ReadUInt32LittleEndian(lenBuf);
            if (len < WireHeaderSize || len > MaxFrameLen) return false;
            var body = new byte[len];
            stream.ReadExactly(body);
            return TryDecodeWire(body, out frame);
        }
        catch (Exception e) when (e is IOException or EndOfStreamException or ObjectDisposedException)
        {
            return false;
        }
    }
}
