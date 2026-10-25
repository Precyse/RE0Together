using System.Buffers.Binary;

namespace CoopLauncher;

/// <summary>Reads bytes of a Windows executable by the virtual address they have once loaded (read only: the section table maps the
/// address to the file offset).</summary>
public static class PeImage
{
    private const int DosHeaderSize = 0x40;
    private const int PeOffsetField = 0x3C;
    private const int PeSignature = 0x00004550;
    private const int FileHeaderSize = 20;
    private const int SectionCountField = 6;
    private const int OptionalSizeField = 20;
    private const int Pe32Magic = 0x10B;
    private const int Pe32PlusMagic = 0x20B;
    private const int Pe32ImageBaseField = 28;
    private const int Pe32PlusImageBaseField = 24;
    private const int OptionalHeaderStart = 4 + FileHeaderSize;
    private const int SectionSize = 40;
    private const int SectionVirtualSizeField = 8;
    private const int SectionVirtualAddressField = 12;
    private const int SectionRawSizeField = 16;
    private const int SectionRawPointerField = 20;

    /// <summary>The <paramref name="length"/> bytes at virtual address <paramref name="address"/> (an absolute address, the image base
    /// included), or null when the file is not a PE image or the bytes are not all stored in one section of the file.</summary>
    public static byte[]? ReadAt(string path, uint address, int length)
    {
        try
        {
            using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite);
            var dos = new byte[DosHeaderSize];
            if (stream.Read(dos) != DosHeaderSize) return null;
            var peAt = BinaryPrimitives.ReadInt32LittleEndian(dos.AsSpan(PeOffsetField));
            if (peAt < DosHeaderSize) return null;
            var fileHeader = new byte[OptionalHeaderStart];
            stream.Position = peAt;
            if (stream.Read(fileHeader) != OptionalHeaderStart || BinaryPrimitives.ReadInt32LittleEndian(fileHeader) != PeSignature) return null;
            var sectionCount = BinaryPrimitives.ReadUInt16LittleEndian(fileHeader.AsSpan(SectionCountField));
            var optional = new byte[BinaryPrimitives.ReadUInt16LittleEndian(fileHeader.AsSpan(OptionalSizeField))];
            var sections = new byte[sectionCount * SectionSize];
            if (stream.Read(optional) != optional.Length || stream.Read(sections) != sections.Length) return null;
            if (ImageBase(optional) is not { } imageBase || address < imageBase) return null;
            var rva = address - imageBase;
            for (var at = 0; at < sections.Length; at += SectionSize)
            {
                var section = sections.AsSpan(at, SectionSize);
                var start = BinaryPrimitives.ReadUInt32LittleEndian(section[SectionVirtualAddressField..]);
                var stored = Math.Min(BinaryPrimitives.ReadUInt32LittleEndian(section[SectionVirtualSizeField..]), BinaryPrimitives.ReadUInt32LittleEndian(section[SectionRawSizeField..]));
                if (rva < start || rva + (ulong)length > (ulong)start + stored) continue;
                stream.Position = BinaryPrimitives.ReadUInt32LittleEndian(section[SectionRawPointerField..]) + (long)(rva - start);
                var bytes = new byte[length];
                return stream.Read(bytes) == length ? bytes : null;
            }
            return null;
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            return null;
        }
    }

    private static ulong? ImageBase(ReadOnlySpan<byte> optional)
    {
        if (optional.Length < sizeof(ushort)) return null;
        return BinaryPrimitives.ReadUInt16LittleEndian(optional) switch
        {
            Pe32Magic when optional.Length >= Pe32ImageBaseField + sizeof(uint) => BinaryPrimitives.ReadUInt32LittleEndian(optional[Pe32ImageBaseField..]),
            Pe32PlusMagic when optional.Length >= Pe32PlusImageBaseField + sizeof(ulong) => BinaryPrimitives.ReadUInt64LittleEndian(optional[Pe32PlusImageBaseField..]),
            _ => null,
        };
    }
}
