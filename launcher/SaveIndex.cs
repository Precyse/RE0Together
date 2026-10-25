using System.Buffers.Binary;
using System.Globalization;
using System.Security.Cryptography;
using System.Text;

namespace CoopLauncher;

/// <summary>One save slot of a game's save file, as the game's own load list reads it. <see cref="Hash"/> identifies the slot's bytes
/// (FNV-1a over the whole slot, 8 lowercase hex digits): the adapter computes the same, so both machines can tell they hold the same slot.</summary>
public sealed record SaveSlot(
    int Index, bool Used, int Area, bool Cleared, int SaveCount, int Difficulty, TimeSpan PlayTime, bool Wesker, int Room, string Hash)
{
    private const string SlotWord = "Slot";
    private const string AreaWord = "Area";
    private const string ClearedWord = "Cleared";
    private const string WeskerWord = "Wesker";
    private const string LabelGap = " / ";
    private static readonly string[] DifficultyNames = ["Easy", "Normal", "Hard"];

    /// <summary>The slot the way the picker lists it, numbered from 1: <c>Slot 3 / Normal / 2:14:05 / Area 4</c>.</summary>
    public string Label
    {
        get
        {
            var parts = new List<string> { $"{SlotWord} {Index + 1}" };
            if (Difficulty >= 0 && Difficulty < DifficultyNames.Length) parts.Add(DifficultyNames[Difficulty]);
            parts.Add(string.Create(CultureInfo.InvariantCulture, $"{(int)PlayTime.TotalHours}:{PlayTime.Minutes:00}:{PlayTime.Seconds:00}"));
            parts.Add(Cleared ? ClearedWord : $"{AreaWord} {Area}");
            if (Wesker) parts.Add(WeskerWord);
            return string.Join(LabelGap, parts);
        }
    }
}

/// <summary>The slots of a game's save file, or why they could not be read. Pure: the file's bytes and the key come from the caller.
/// The file is one Blowfish-encrypted container (<see cref="Blowfish"/>): a 0x40 byte header whose SHA-1 covers the rest, a table of
/// entry offsets, and per entry a 16 byte header (name hash = the complement of the CRC-32 of the name, type, size) and its data.
/// A slot entry holds the game's slot image, whose first fields are what the load list shows.</summary>
public sealed class SaveIndex
{
    private const int HeaderSize = 0x40;
    private const int EntryTableSizeField = 8;
    private const int ChecksumField = 0x0C;
    private const int ChecksumSize = 20;
    private const int PayloadSizeField = 0x20;
    private const int EntryHeaderSize = 16;
    private const int EntryNameHashField = 0;
    private const int EntrySizeField = 8;
    private const int OffsetSize = sizeof(uint);
    private const int AreaField = 4;
    private const int SaveCountField = 5;
    private const int WeskerField = 8;
    private const int UsedFieldsStart = 4;
    private const int UsedFieldsEnd = 8;
    private const int DifficultyField = 0x14;
    private const int PlayFramesField = 0x1C;
    private const int RoomField = 0x1C778;
    private const byte ClearedArea = 0x0C;
    private const int MinSlotSize = RoomField + sizeof(int);
    private const float FramesPerSecond = 60f;
    private const double MaxPlaySeconds = 360_000;
    private const uint Crc32Polynomial = 0xEDB88320;
    private const int ByteValues = 256;
    private const int ByteMask = 0xFF;
    private const int BitsPerByte = 8;

    private static readonly uint[] Crc32Table = BuildCrcTable();

    private SaveIndex(IReadOnlyList<SaveSlot> slots, string? failure)
    {
        Slots = slots;
        Failure = failure;
    }

    public IReadOnlyList<SaveSlot> Slots { get; }

    /// <summary>Why the file could not be read (the words of the log line); null when it could.</summary>
    public string? Failure { get; }

    /// <summary>No slots and nothing wrong: there is no save to read yet.</summary>
    public static SaveIndex Empty { get; } = new([], null);

    public static SaveIndex Failed(string reason) => new([], reason);

    /// <summary>Decrypts the file with <paramref name="key"/> and lists its slots. A file of the wrong size, a header checksum that does not match
    /// (the key is wrong or the file damaged) or an entry that is missing, out of range or of the wrong size is a failure with no slots.</summary>
    public static SaveIndex Parse(ReadOnlySpan<byte> file, ReadOnlySpan<byte> key, SaveIndexProfile layout)
    {
        if (file.Length < HeaderSize || file.Length % Blowfish.BlockSize != 0) return Failed($"save file has the wrong size ({file.Length} bytes)");
        if (layout.SlotSize < MinSlotSize) return Failed($"profile slot size {layout.SlotSize} is too small");
        var data = new Blowfish(key).DecryptEcb(file);
        var payload = BinaryPrimitives.ReadUInt32LittleEndian(data.AsSpan(PayloadSizeField));
        if (payload > data.Length - HeaderSize) return Failed("save header does not match the key or the file is damaged");
        if (!ChecksumMatches(data, (int)payload)) return Failed("save checksum does not match: the key is wrong or the file is damaged");
        var entries = EntriesOf(data);
        var slots = new List<SaveSlot>();
        for (var index = 0; index < layout.Slots; index++)
        {
            var name = string.Format(CultureInfo.InvariantCulture, layout.Entry, index);
            if (!entries.TryGetValue(NameHash(name), out var entry)) return Failed($"save has no {name}");
            if (entry.Length != layout.SlotSize) return Failed($"save {name} has the wrong size ({entry.Length} bytes)");
            slots.Add(SlotOf(index, data.AsSpan(entry.Offset, entry.Length)));
        }
        return new SaveIndex(slots, null);
    }

    /// <summary>The slot's hash from its bytes: FNV-1a, 8 lowercase hex digits.</summary>
    public static string HashOf(ReadOnlySpan<byte> slot) => Fnv1a.Hash(slot).ToString("x8", CultureInfo.InvariantCulture);

    /// <summary>The name hash a container entry is stored under: the complement of the zlib CRC-32 of the ASCII name.</summary>
    public static uint NameHash(string name)
    {
        var crc = uint.MaxValue;
        foreach (var next in Encoding.ASCII.GetBytes(name)) crc = (crc >> BitsPerByte) ^ Crc32Table[(crc ^ next) & ByteMask];
        return crc;
    }

    private static bool ChecksumMatches(byte[] data, int payload)
    {
        var digest = SHA1.HashData(data.AsSpan(HeaderSize, payload));
        for (var at = 0; at < ChecksumSize; at += OffsetSize)
        {
            if (BinaryPrimitives.ReadUInt32BigEndian(digest.AsSpan(at)) != BinaryPrimitives.ReadUInt32LittleEndian(data.AsSpan(ChecksumField + at))) return false;
        }
        return true;
    }

    private static Dictionary<uint, (int Offset, int Length)> EntriesOf(byte[] data)
    {
        var entries = new Dictionary<uint, (int, int)>();
        var count = BinaryPrimitives.ReadUInt32LittleEndian(data.AsSpan(EntryTableSizeField)) / OffsetSize;
        for (var i = 0; i < count && HeaderSize + (i + 1) * OffsetSize <= data.Length; i++)
        {
            var at = BinaryPrimitives.ReadUInt32LittleEndian(data.AsSpan(HeaderSize + (int)i * OffsetSize));
            if (at > data.Length - EntryHeaderSize) continue;
            var size = BinaryPrimitives.ReadUInt32LittleEndian(data.AsSpan((int)at + EntrySizeField));
            if (size > data.Length - at - EntryHeaderSize) continue;
            entries[BinaryPrimitives.ReadUInt32LittleEndian(data.AsSpan((int)at + EntryNameHashField))] = ((int)at + EntryHeaderSize, (int)size);
        }
        return entries;
    }

    private static SaveSlot SlotOf(int index, ReadOnlySpan<byte> slot)
    {
        var used = slot[UsedFieldsStart..UsedFieldsEnd].IndexOfAnyExcept((byte)0) >= 0;
        var frames = BinaryPrimitives.ReadSingleLittleEndian(slot[PlayFramesField..]);
        var seconds = float.IsFinite(frames) ? Math.Clamp(Math.Round(frames / FramesPerSecond), 0, MaxPlaySeconds) : 0;
        return new SaveSlot(
            index, used, slot[AreaField], slot[AreaField] == ClearedArea, slot[SaveCountField],
            (int)BinaryPrimitives.ReadUInt32LittleEndian(slot[DifficultyField..]), TimeSpan.FromSeconds(seconds),
            slot[WeskerField] != 0, BinaryPrimitives.ReadInt32LittleEndian(slot[RoomField..]), HashOf(slot));
    }

    private static uint[] BuildCrcTable()
    {
        var table = new uint[ByteValues];
        for (uint i = 0; i < table.Length; i++)
        {
            var value = i;
            for (var bit = 0; bit < BitsPerByte; bit++) value = (value & 1) != 0 ? (value >> 1) ^ Crc32Polynomial : value >> 1;
            table[i] = value;
        }
        return table;
    }
}
