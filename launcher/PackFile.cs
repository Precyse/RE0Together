using System.Buffers.Binary;
using System.Text;

namespace CoopLauncher;

/// <summary>Reads one named entry of a Saints Row 2 packfile (<c>.vpp_pc</c>, version 4), read-only. Layout: a header whose flags
/// are at 0x150 and counts at 0x154 (files, total size, then the sizes of the directory, the names, the extensions and the data,
/// and the condensed size), then the directory, the names, the extensions and the data, each starting on a 0x800 boundary. A
/// compressed or condensed pack is refused.</summary>
public static class PackFile
{
    private const uint Magic = 0x51890ACE;
    private const uint SupportedVersion = 4;
    private const int VersionOffset = 4;
    private const int FlagsOffset = 0x150;
    private const int CountsOffset = 0x154;
    private const int HeaderSize = 0x170;
    private const long Alignment = 0x800;
    private const int EntrySize = 28;
    private const uint CompressedFlag = 1;
    private const uint NotCompressed = 0xFFFFFFFF;
    private const byte NameEnd = 0;

    private readonly record struct Layout(uint Files, uint DirectorySize, long NamesOffset, uint NamesSize, long ExtensionsOffset, uint ExtensionsSize, long DataOffset);

    /// <summary>The bytes of entry <paramref name="name"/> (name and extension, compared without case), or null when the pack has no
    /// such entry. Throws <see cref="InvalidDataException"/> for a file that is not a plain version 4 pack.</summary>
    public static byte[]? ReadEntry(string path, string name)
    {
        using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite);
        var layout = ReadLayout(stream);
        var directory = ReadAt(stream, Alignment, layout.DirectorySize);
        var names = ReadAt(stream, layout.NamesOffset, layout.NamesSize);
        var extensions = ReadAt(stream, layout.ExtensionsOffset, layout.ExtensionsSize);
        for (var index = 0; index < layout.Files; index++)
        {
            var entry = directory.AsSpan(index * EntrySize, EntrySize);
            var entryName = CString(names, U32(entry, 0)) + "." + CString(extensions, U32(entry, 4));
            if (!entryName.Equals(name, StringComparison.OrdinalIgnoreCase)) continue;
            if (U32(entry, 20) != NotCompressed) throw new InvalidDataException($"{Path.GetFileName(path)}: {entryName} is compressed");
            return ReadAt(stream, layout.DataOffset + U32(entry, 12), U32(entry, 16));
        }
        return null;
    }

    private static Layout ReadLayout(FileStream stream)
    {
        var header = ReadAt(stream, 0, HeaderSize);
        if (U32(header, 0) != Magic || U32(header, VersionOffset) != SupportedVersion)
            throw new InvalidDataException("not a version 4 packfile");
        if ((U32(header, FlagsOffset) & CompressedFlag) != 0 || U32(header, CountsOffset + 24) != NotCompressed)
            throw new InvalidDataException("a compressed or condensed packfile is not supported");
        var files = U32(header, CountsOffset);
        var directorySize = U32(header, CountsOffset + 8);
        var namesSize = U32(header, CountsOffset + 12);
        var extensionsSize = U32(header, CountsOffset + 16);
        if (directorySize != files * (long)EntrySize) throw new InvalidDataException("the packfile directory has the wrong size");
        var namesOffset = Alignment + Aligned(directorySize);
        var extensionsOffset = namesOffset + Aligned(namesSize);
        return new Layout(files, directorySize, namesOffset, namesSize, extensionsOffset, extensionsSize, extensionsOffset + Aligned(extensionsSize));
    }

    private static long Aligned(long size) => (size + Alignment - 1) / Alignment * Alignment;

    private static uint U32(ReadOnlySpan<byte> bytes, int offset) => BinaryPrimitives.ReadUInt32LittleEndian(bytes[offset..]);

    private static byte[] ReadAt(FileStream stream, long offset, long length)
    {
        if (length > stream.Length - offset) throw new InvalidDataException("the packfile ends before its data");
        var bytes = new byte[length];
        stream.Seek(offset, SeekOrigin.Begin);
        stream.ReadExactly(bytes);
        return bytes;
    }

    private static string CString(byte[] bytes, uint offset)
    {
        if (offset >= bytes.Length) throw new InvalidDataException("a packfile name lies outside its name table");
        var end = Array.IndexOf(bytes, NameEnd, (int)offset);
        if (end < 0) throw new InvalidDataException("a packfile name is not terminated");
        return Encoding.Latin1.GetString(bytes, (int)offset, end - (int)offset);
    }
}
