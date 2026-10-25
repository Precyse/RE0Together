using System.Buffers.Binary;
using System.Security.Cryptography;
using System.Text;
using CoopLauncher;

namespace LauncherTests;

/// <summary>A save slot the tests put in a synthetic save: the fields the picker lists (the rest of the slot is filled from the index).</summary>
public sealed record SyntheticSlot(int Index, byte Area, byte Count, uint Difficulty, float Frames, bool Wesker, int Room);

/// <summary>A synthetic save file in the real container format (header with the SHA-1, entry table, slot entries, Blowfish ECB with a test key)
/// and a synthetic executable holding the key at a virtual address, so the tests never need a real save or the game's key.</summary>
public static class SaveFixture
{
    public const string KeyText = "test-key-for-the-synthetic-save!";
    public const uint KeyAddress = 0x00402010;
    public const string ExeName = "game.exe";
    public const int SlotCount = 4;
    public const int SlotSize = 116_816;

    private const int HeaderSize = 0x40;
    private const int EntryHeaderSize = 16;
    private const uint ImageBase = 0x00400000;
    private const uint SectionRva = 0x2000;
    private const uint SectionRawPointer = 0x400;
    private const int SectionSize = 0x200;
    private const int PeHeaderAt = 0x80;
    private const ushort Pe32Magic = 0x10B;
    private const int Pe32OptionalSize = 0xE0;
    private const int EntryTypeField = 4;
    private const int EntryType = 0xC;
    private const int FillStart = 0x20;
    private const int FillEnd = 0x1000;
    private const int FillMultiplier = 7;

    public static readonly byte[] Key = Encoding.ASCII.GetBytes(KeyText);

    public static readonly SaveIndexProfile Layout = new("data0.bin", "0x" + KeyAddress.ToString("x"), Key.Length, "GAMEDATA{0}", SlotCount, SlotSize);

    /// <summary>Slot 0: used, Normal, 8200 frames (2:17), area 4, room 0x59, 11 saves. Slot 1: used, Hard, Wesker, 27:46:40. Slots 2 and 3: empty.</summary>
    public static readonly SyntheticSlot[] Slots =
    [
        new(0, 4, 11, 1, 8200f, false, 0x59),
        new(1, 13, 3, 2, 6_000_000f, true, 0x5B),
    ];

    /// <summary>The encrypted file for these slots (the others are empty), under <paramref name="key"/>.</summary>
    public static byte[] Build(IReadOnlyList<SyntheticSlot> slots, ReadOnlySpan<byte> key)
    {
        var entries = new List<(uint Hash, byte[] Data)> { (SaveIndex.NameHash("VERSION"), new byte[4]) };
        for (var index = 0; index < SlotCount; index++) entries.Add((SaveIndex.NameHash($"GAMEDATA{index}"), SlotBytes(slots.FirstOrDefault(slot => slot.Index == index))));
        return Encrypt(Container(entries), key);
    }

    /// <summary>The same container built from raw entries (a missing or wrongly sized slot).</summary>
    public static byte[] BuildEntries(IReadOnlyList<(string Name, byte[] Data)> named, ReadOnlySpan<byte> key) =>
        Encrypt(Container(named.Select(entry => (SaveIndex.NameHash(entry.Name), entry.Data)).ToList()), key);

    public static byte[] SlotBytes(SyntheticSlot? slot)
    {
        var bytes = new byte[SlotSize];
        if (slot == null) return bytes;
        bytes[4] = slot.Area;
        bytes[5] = slot.Count;
        bytes[8] = (byte)(slot.Wesker ? 1 : 0);
        BinaryPrimitives.WriteUInt32LittleEndian(bytes.AsSpan(0x14), slot.Difficulty);
        BinaryPrimitives.WriteSingleLittleEndian(bytes.AsSpan(0x1C), slot.Frames);
        BinaryPrimitives.WriteInt32LittleEndian(bytes.AsSpan(0x1C778), slot.Room);
        for (var at = FillStart; at < FillEnd; at++) bytes[at] = (byte)(at * FillMultiplier + slot.Index);
        return bytes;
    }

    /// <summary>A 32-bit PE executable with one section that holds <paramref name="key"/> at <see cref="KeyAddress"/>.</summary>
    public static byte[] Exe(ReadOnlySpan<byte> key, uint address = KeyAddress)
    {
        var exe = new byte[SectionRawPointer + SectionSize];
        BinaryPrimitives.WriteInt32LittleEndian(exe.AsSpan(0x3C), PeHeaderAt);
        BinaryPrimitives.WriteInt32LittleEndian(exe.AsSpan(PeHeaderAt), 0x00004550);
        BinaryPrimitives.WriteUInt16LittleEndian(exe.AsSpan(PeHeaderAt + 6), 1);
        BinaryPrimitives.WriteUInt16LittleEndian(exe.AsSpan(PeHeaderAt + 20), Pe32OptionalSize);
        var optional = PeHeaderAt + 24;
        BinaryPrimitives.WriteUInt16LittleEndian(exe.AsSpan(optional), Pe32Magic);
        BinaryPrimitives.WriteUInt32LittleEndian(exe.AsSpan(optional + 28), ImageBase);
        var section = optional + Pe32OptionalSize;
        Encoding.ASCII.GetBytes(".rdata").CopyTo(exe, section);
        BinaryPrimitives.WriteUInt32LittleEndian(exe.AsSpan(section + 8), SectionSize);
        BinaryPrimitives.WriteUInt32LittleEndian(exe.AsSpan(section + 12), SectionRva);
        BinaryPrimitives.WriteUInt32LittleEndian(exe.AsSpan(section + 16), SectionSize);
        BinaryPrimitives.WriteUInt32LittleEndian(exe.AsSpan(section + 20), SectionRawPointer);
        key.CopyTo(exe.AsSpan((int)SectionRawPointer + (int)(address - ImageBase - SectionRva)));
        return exe;
    }

    /// <summary>A game folder with the synthetic executable and, in the session folder, the file built from <paramref name="slots"/>.</summary>
    public static string GameFolder(string root, IReadOnlyList<SyntheticSlot> slots)
    {
        Directory.CreateDirectory(Path.Combine(root, "coop", "session"));
        File.WriteAllBytes(Path.Combine(root, ExeName), Exe(Key));
        File.WriteAllBytes(Path.Combine(root, "coop", "session", Layout.File), Build(slots, Key));
        return root;
    }

    private static byte[] Container(List<(uint Hash, byte[] Data)> entries)
    {
        var tableBytes = entries.Count * sizeof(uint);
        var payload = new List<byte>();
        var offsets = new List<uint>();
        var at = HeaderSize + tableBytes;
        foreach (var (hash, data) in entries)
        {
            offsets.Add((uint)at);
            var entry = new byte[EntryHeaderSize + data.Length];
            BinaryPrimitives.WriteUInt32LittleEndian(entry, hash);
            BinaryPrimitives.WriteUInt32LittleEndian(entry.AsSpan(EntryTypeField), EntryType);
            BinaryPrimitives.WriteUInt32LittleEndian(entry.AsSpan(8), (uint)data.Length);
            data.CopyTo(entry, EntryHeaderSize);
            payload.AddRange(entry);
            at += entry.Length;
        }
        var body = new List<byte>();
        foreach (var offset in offsets) body.AddRange(BitConverter.GetBytes(offset));
        body.AddRange(payload);
        var padding = (Blowfish.BlockSize - (HeaderSize + body.Count) % Blowfish.BlockSize) % Blowfish.BlockSize;
        body.AddRange(new byte[padding]);
        var file = new byte[HeaderSize + body.Count];
        BinaryPrimitives.WriteUInt32LittleEndian(file, 1);
        BinaryPrimitives.WriteUInt32LittleEndian(file.AsSpan(4), 1);
        BinaryPrimitives.WriteUInt32LittleEndian(file.AsSpan(8), (uint)tableBytes);
        BinaryPrimitives.WriteUInt32LittleEndian(file.AsSpan(0x20), (uint)body.Count);
        body.CopyTo(file, HeaderSize);
        var digest = SHA1.HashData(file.AsSpan(HeaderSize));
        for (var word = 0; word < digest.Length; word += sizeof(uint)) BinaryPrimitives.WriteUInt32LittleEndian(file.AsSpan(0x0C + word), BinaryPrimitives.ReadUInt32BigEndian(digest.AsSpan(word)));
        return file;
    }

    private static byte[] Encrypt(byte[] plain, ReadOnlySpan<byte> key) => new Blowfish(key).EncryptEcb(plain);
}
