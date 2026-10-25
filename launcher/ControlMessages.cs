using System.Buffers.Binary;
using System.Text;

namespace CoopLauncher;

/// <summary>Builders and parsers for launcher-owned control payloads (types below 0x0100).</summary>
public static class ControlMessages
{
    private const int MaxOptionText = 32;
    private const byte MaxAscii = 0x7F;
    private const char FirstPrintable = ' ';
    private const char LastPrintable = '~';
    private const char OptionSeparator = '=';

    public static Frame Welcome(byte localSlot, byte hostSlot, byte maxPlayers, uint epoch, ulong localId)
    {
        var p = new byte[15];
        p[0] = localSlot;
        p[1] = hostSlot;
        p[2] = maxPlayers;
        BinaryPrimitives.WriteUInt32LittleEndian(p.AsSpan(3), epoch);
        BinaryPrimitives.WriteUInt64LittleEndian(p.AsSpan(7), localId);
        return Framing.Reliable(Msg.Welcome, p);
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
        return Framing.Reliable(Msg.PeerUp, p);
    }

    public static Frame PeerDown(byte slot) => Framing.Reliable(Msg.PeerDown, [slot]);

    public static Frame Reject(string reason)
    {
        var text = Encoding.ASCII.GetBytes(reason);
        var p = new byte[1 + text.Length];
        p[0] = (byte)text.Length;
        text.CopyTo(p, 1);
        return Framing.Reliable(Msg.Reject, p);
    }

    public static Frame Ping(ulong micros) => MakeTimestamp(Msg.Ping, micros);

    public static Frame Pong(ulong echoedMicros) => MakeTimestamp(Msg.Pong, echoedMicros);

    public static Frame PeerStats(byte slot, ushort rttMs)
    {
        var p = new byte[3];
        p[0] = slot;
        BinaryPrimitives.WriteUInt16LittleEndian(p.AsSpan(1), rttMs);
        return Framing.Reliable(Msg.PeerStats, p);
    }

    public static Frame Heartbeat() => Framing.Reliable(Msg.Heartbeat, []);

    /// <summary>LOBBY_STATE: the lobby as text for the adapter's own co-op page (see <see cref="LobbyStateText"/>).</summary>
    public static Frame LobbyState(string text) => Framing.Reliable(Msg.LobbyState, Encoding.UTF8.GetBytes(text));

    /// <summary>OPTION_SET: ASCII <c>key=value</c>, a key of 1 to 32 characters of [a-z0-9_] and a value of 0 to 32 printable
    /// characters without ';' or '='. False for anything else.</summary>
    public static bool TryParseOptionSet(byte[] payload, out string key, out string value)
    {
        key = "";
        value = "";
        if (payload.Any(b => b > MaxAscii)) return false;
        var parts = Encoding.ASCII.GetString(payload).Split(OptionSeparator, 2);
        if (parts.Length != 2 || !IsOptionKey(parts[0]) || !IsOptionValue(parts[1])) return false;
        key = parts[0];
        value = parts[1];
        return true;
    }

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

    private static bool IsOptionKey(string key) =>
        key.Length is >= 1 and <= MaxOptionText && key.All(c => c is (>= 'a' and <= 'z') or (>= '0' and <= '9') or '_');

    private static bool IsOptionValue(string value) =>
        value.Length <= MaxOptionText && value.All(c => c is >= FirstPrintable and <= LastPrintable && c != ';' && c != OptionSeparator);

    private static Frame MakeTimestamp(ushort type, ulong micros)
    {
        var p = new byte[8];
        BinaryPrimitives.WriteUInt64LittleEndian(p, micros);
        return new Frame(type, Flags: 0, Framing.NoSlot, p);
    }
}
