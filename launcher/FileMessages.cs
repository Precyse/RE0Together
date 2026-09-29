using System.Buffers.Binary;
using System.Text;

namespace CoopLauncher;

public readonly record struct FileBegin(uint Id, uint Size, byte[] Hash, string Name);

public readonly record struct FileChunk(uint Id, uint Offset, ReadOnlyMemory<byte> Data);

/// <summary>Builders and parsers for the FILE_BEGIN / CHUNK / END / ACK payloads (contract: Save sync).</summary>
public static class FileMessages
{
    public const int HashSize = 32;

    private const byte NoSlot = 0;
    private const int IdSize = 4;
    private const int BeginFixedSize = IdSize + 4 + HashSize + 1;
    private const int ChunkHeaderSize = IdSize + 4;

    public static Frame Begin(FileBegin begin)
    {
        var name = Encoding.ASCII.GetBytes(begin.Name);
        var p = new byte[BeginFixedSize + name.Length];
        BinaryPrimitives.WriteUInt32LittleEndian(p, begin.Id);
        BinaryPrimitives.WriteUInt32LittleEndian(p.AsSpan(IdSize), begin.Size);
        begin.Hash.CopyTo(p, IdSize + 4);
        p[BeginFixedSize - 1] = (byte)name.Length;
        name.CopyTo(p, BeginFixedSize);
        return Make(Msg.FileBegin, p);
    }

    public static Frame Chunk(uint id, uint offset, ReadOnlySpan<byte> data)
    {
        var p = new byte[ChunkHeaderSize + data.Length];
        BinaryPrimitives.WriteUInt32LittleEndian(p, id);
        BinaryPrimitives.WriteUInt32LittleEndian(p.AsSpan(IdSize), offset);
        data.CopyTo(p.AsSpan(ChunkHeaderSize));
        return Make(Msg.FileChunk, p);
    }

    public static Frame End(uint id)
    {
        var p = new byte[IdSize];
        BinaryPrimitives.WriteUInt32LittleEndian(p, id);
        return Make(Msg.FileEnd, p);
    }

    public static Frame Ack(uint id, bool ok)
    {
        var p = new byte[IdSize + 1];
        BinaryPrimitives.WriteUInt32LittleEndian(p, id);
        p[IdSize] = ok ? (byte)1 : (byte)0;
        return Make(Msg.FileAck, p);
    }

    public static bool TryParseBegin(byte[] payload, out FileBegin begin)
    {
        begin = default;
        if (payload.Length < BeginFixedSize) return false;
        int nameLength = payload[BeginFixedSize - 1];
        if (payload.Length < BeginFixedSize + nameLength) return false;
        begin = new FileBegin(
            BinaryPrimitives.ReadUInt32LittleEndian(payload),
            BinaryPrimitives.ReadUInt32LittleEndian(payload.AsSpan(IdSize)),
            payload.AsSpan(IdSize + 4, HashSize).ToArray(),
            Encoding.ASCII.GetString(payload, BeginFixedSize, nameLength));
        return true;
    }

    public static bool TryParseChunk(byte[] payload, out FileChunk chunk)
    {
        chunk = default;
        if (payload.Length < ChunkHeaderSize) return false;
        chunk = new FileChunk(
            BinaryPrimitives.ReadUInt32LittleEndian(payload),
            BinaryPrimitives.ReadUInt32LittleEndian(payload.AsSpan(IdSize)),
            payload.AsMemory(ChunkHeaderSize));
        return true;
    }

    public static bool TryParseId(byte[] payload, out uint id)
    {
        id = 0;
        if (payload.Length < IdSize) return false;
        id = BinaryPrimitives.ReadUInt32LittleEndian(payload);
        return true;
    }

    public static bool TryParseAck(byte[] payload, out uint id, out bool ok)
    {
        ok = false;
        if (!TryParseId(payload, out id) || payload.Length < IdSize + 1) return false;
        ok = payload[IdSize] != 0;
        return true;
    }

    private static Frame Make(ushort type, byte[] payload) => new(type, Framing.FlagReliable, NoSlot, payload);
}
