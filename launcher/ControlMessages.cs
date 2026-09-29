using System.Buffers.Binary;
using System.Text;

namespace CoopLauncher;

/// <summary>Builders and parsers for launcher-owned control payloads (types below 0x0100).</summary>
public static class ControlMessages
{
    private const byte NoSlot = 0;

    public static Frame Welcome(byte localSlot, byte hostSlot, byte maxPlayers, uint epoch)
    {
        var p = new byte[7];
        p[0] = localSlot;
        p[1] = hostSlot;
        p[2] = maxPlayers;
        BinaryPrimitives.WriteUInt32LittleEndian(p.AsSpan(3), epoch);
        return Make(Msg.Welcome, p);
    }

    public static Frame PeerUp(byte slot, ulong steamId, string name)
    {
        var nameBytes = Encoding.UTF8.GetBytes(name);
        var length = Math.Min(nameBytes.Length, byte.MaxValue);
        var p = new byte[1 + 8 + 1 + length];
        p[0] = slot;
        BinaryPrimitives.WriteUInt64LittleEndian(p.AsSpan(1), steamId);
        p[9] = (byte)length;
        Array.Copy(nameBytes, 0, p, 10, length);
        return Make(Msg.PeerUp, p);
    }

    public static Frame PeerDown(byte slot) => Make(Msg.PeerDown, [slot]);

    public static Frame Reject(string reason)
    {
        var text = Encoding.ASCII.GetBytes(reason);
        var p = new byte[1 + text.Length];
        p[0] = (byte)text.Length;
        text.CopyTo(p, 1);
        return Make(Msg.Reject, p);
    }

    public static Frame Ping(ulong micros) => MakeTimestamp(Msg.Ping, micros);

    public static Frame Pong(ulong echoedMicros) => MakeTimestamp(Msg.Pong, echoedMicros);

    public static Frame PeerStats(byte slot, ushort rttMs)
    {
        var p = new byte[3];
        p[0] = slot;
        BinaryPrimitives.WriteUInt16LittleEndian(p.AsSpan(1), rttMs);
        return Make(Msg.PeerStats, p);
    }

    public static Frame Heartbeat() => Make(Msg.Heartbeat, []);

    public static bool TryParseHello(byte[] payload, out ushort proto, out string gameId)
    {
        proto = 0;
        gameId = "";
        if (payload.Length < 3) return false;
        proto = BinaryPrimitives.ReadUInt16LittleEndian(payload);
        int length = payload[2];
        if (payload.Length < 3 + length) return false;
        gameId = Encoding.ASCII.GetString(payload, 3, length);
        return true;
    }

    public static bool TryParseTimestamp(byte[] payload, out ulong micros)
    {
        micros = 0;
        if (payload.Length < 8) return false;
        micros = BinaryPrimitives.ReadUInt64LittleEndian(payload);
        return true;
    }

    private static Frame MakeTimestamp(ushort type, ulong micros)
    {
        var p = new byte[8];
        BinaryPrimitives.WriteUInt64LittleEndian(p, micros);
        return new Frame(type, Flags: 0, NoSlot, p);
    }

    private static Frame Make(ushort type, byte[] payload) =>
        new(type, Framing.FlagReliable, NoSlot, payload);
}
