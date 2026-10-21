using System.IO.Compression;

namespace CoopLauncher;

/// <summary>A decoded texture: Width x Height pixels in the memory order of a 32 bit ARGB bitmap (blue, green, red, alpha).</summary>
public sealed record DecodedTexture(int Width, int Height, byte[] Bgra);

/// <summary>Reader of the MT Framework archive (ARC, version 7) the game's UI art ships in. The file is only read, opened
/// shared, and only the table and the one entry asked for are read.</summary>
public static class MtArc
{
    private const int HeaderSize = 8;
    private const int NameSize = 64;
    private const int EntrySize = NameSize + 16;
    private const int CompressedPosition = NameSize + sizeof(uint);
    private const int SizePosition = CompressedPosition + sizeof(uint);
    private const int OffsetPosition = SizePosition + sizeof(uint);
    private const int CountOffset = 6;
    private const uint SizeMask = 0x1FFFFFFF;
    private const char PathSeparator = '\\';
    private static readonly byte[] Magic = "ARC\0"u8.ToArray();

    /// <summary>The body of the entry whose name ends with <paramref name="name"/> (the names are backslash paths without
    /// an extension), inflated when it is stored compressed; null when the file has no such entry. A file that is not an
    /// archive, or whose table or entry lies outside the file, throws <see cref="InvalidDataException"/>.</summary>
    public static byte[]? ReadEntry(string path, string name)
    {
        using var file = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
        var header = ReadExactly(file, HeaderSize);
        if (!header.AsSpan(0, Magic.Length).SequenceEqual(Magic)) throw new InvalidDataException($"{path} is not an archive");
        var count = BitConverter.ToUInt16(header, CountOffset);
        var table = ReadExactly(file, count * EntrySize);
        for (var i = 0; i < count; i++)
        {
            var entry = table.AsSpan(i * EntrySize, EntrySize);
            if (EntryName(entry[..NameSize]) != name) continue;
            var compressed = BitConverter.ToUInt32(entry[CompressedPosition..]);
            var size = BitConverter.ToUInt32(entry[SizePosition..]) & SizeMask;
            var offset = BitConverter.ToUInt32(entry[OffsetPosition..]);
            if (offset + (long)compressed > file.Length) throw new InvalidDataException($"{path}: entry {name} lies outside the file");
            file.Seek(offset, SeekOrigin.Begin);
            var stored = ReadExactly(file, (int)compressed);
            return compressed == size ? stored : Inflate(stored, (int)size);
        }
        return null;
    }

    private static string EntryName(ReadOnlySpan<byte> field)
    {
        var end = field.IndexOf((byte)0);
        var text = System.Text.Encoding.ASCII.GetString(end < 0 ? field : field[..end]);
        return text[(text.LastIndexOf(PathSeparator) + 1)..];
    }

    private static byte[] ReadExactly(Stream stream, int length)
    {
        var bytes = new byte[length];
        try
        {
            stream.ReadExactly(bytes);
        }
        catch (EndOfStreamException e)
        {
            throw new InvalidDataException("the archive ends early", e);
        }
        return bytes;
    }

    private static byte[] Inflate(byte[] stored, int size)
    {
        using var inflater = new ZLibStream(new MemoryStream(stored), CompressionMode.Decompress);
        return ReadExactly(inflater, size);
    }
}

/// <summary>Reader of the MT Framework texture (TEX, version 157): the first mip level of a BC3 image (format 0x18), or of
/// a BC3 image that holds YCbCr (format 0x2B). The block decoder is written out here, so a texture decodes the same on
/// every machine.</summary>
public static class MtTexture
{
    private const int MipOffsetPosition = 16;
    private const int DimensionsPosition = 8;
    private const int FormatPosition = 12;
    private const int MinHeaderSize = MipOffsetPosition + sizeof(uint);
    private const int WidthShift = 6;
    private const uint WidthMask = 0x1FFF;
    private const int HeightShift = 19;
    private const int FormatShift = 8;
    private const uint FormatMask = 0xFF;
    private const byte FormatBc3 = 0x18;
    private const byte FormatBc3YCbCr = 0x2B;
    private const int BlockSize = 4;
    private const int BlockBytes = 16;
    private const int AlphaBlockBytes = 8;
    private const int BytesPerPixel = 4;
    private const int BitsPerByte = 8;
    private const int Rgb565RedShift = 11;
    private const int Rgb565GreenShift = 5;
    private const int Rgb565GreenMask = 63;
    private const int Rgb565BlueMask = 31;
    private const int ChromaZero = 128;
    private const float RedFromCr = 1.402f;
    private const float GreenFromCb = 0.344136f;
    private const float GreenFromCr = 0.714136f;
    private const float BlueFromCb = 1.772f;
    private const int MaxChannel = 255;
    private static readonly byte[] Magic = "TEX\0"u8.ToArray();

    /// <summary>The image of a texture body. A body that is not a texture of a known format, or too short for its size,
    /// throws <see cref="InvalidDataException"/>.</summary>
    public static DecodedTexture Decode(byte[] body)
    {
        if (body.Length < MinHeaderSize || !body.AsSpan(0, Magic.Length).SequenceEqual(Magic)) throw new InvalidDataException("not a texture");
        var dimensions = BitConverter.ToUInt32(body, DimensionsPosition);
        var width = (int)((dimensions >> WidthShift) & WidthMask);
        var height = (int)(dimensions >> HeightShift);
        var format = (byte)((BitConverter.ToUInt32(body, FormatPosition) >> FormatShift) & FormatMask);
        var first = BitConverter.ToUInt32(body, MipOffsetPosition);
        if (format is not (FormatBc3 or FormatBc3YCbCr)) throw new InvalidDataException($"texture format {format:x2} is not supported");
        var blocksWide = (width + BlockSize - 1) / BlockSize;
        var blocksHigh = (height + BlockSize - 1) / BlockSize;
        if (width == 0 || height == 0 || first + (long)blocksWide * blocksHigh * BlockBytes > body.Length)
            throw new InvalidDataException($"a {width}x{height} texture does not fit its {body.Length} bytes");

        var bgra = new byte[width * height * BytesPerPixel];
        for (var by = 0; by < blocksHigh; by++)
            for (var bx = 0; bx < blocksWide; bx++)
                DecodeBlock(body.AsSpan((int)first + (by * blocksWide + bx) * BlockBytes, BlockBytes), bgra, width, height, bx, by, format == FormatBc3YCbCr);
        return new DecodedTexture(width, height, bgra);
    }

    /// <summary>One 16 byte block: 8 bytes of alpha (two end values and 16 three-bit indices), then 8 bytes of colour (two
    /// 565 end values and 16 two-bit indices, always in the four-colour mode).</summary>
    private static void DecodeBlock(ReadOnlySpan<byte> block, byte[] bgra, int width, int height, int bx, int by, bool ycbcr)
    {
        var alpha = AlphaTable(block[0], block[1]);
        var alphaBits = 0UL;
        for (var i = AlphaBlockBytes - 1; i >= 2; i--) alphaBits = (alphaBits << BitsPerByte) | block[i];
        var end0 = BitConverter.ToUInt16(block[AlphaBlockBytes..]);
        var end1 = BitConverter.ToUInt16(block[(AlphaBlockBytes + sizeof(ushort))..]);
        var colourBits = BitConverter.ToUInt32(block[(AlphaBlockBytes + 2 * sizeof(ushort))..]);
        var colours = ColourTable(end0, end1);
        for (var p = 0; p < BlockSize * BlockSize; p++)
        {
            var x = bx * BlockSize + p % BlockSize;
            var y = by * BlockSize + p / BlockSize;
            if (x >= width || y >= height) continue;
            var (r, g, b) = colours[(int)((colourBits >> (2 * p)) & 3)];
            var a = alpha[(int)((alphaBits >> (3 * p)) & 7)];
            var at = (y * width + x) * BytesPerPixel;
            if (ycbcr) WriteYCbCr(bgra, at, luma: a, cr: r - ChromaZero, alpha: g, cb: b - ChromaZero);
            else (bgra[at], bgra[at + 1], bgra[at + 2], bgra[at + 3]) = ((byte)b, (byte)g, (byte)r, (byte)a);
        }
    }

    private static int[] AlphaTable(int a0, int a1)
    {
        var table = new int[8];
        table[0] = a0;
        table[1] = a1;
        if (a0 > a1)
        {
            for (var k = 1; k <= 6; k++) table[k + 1] = ((7 - k) * a0 + k * a1) / 7;
        }
        else
        {
            for (var k = 1; k <= 4; k++) table[k + 1] = ((5 - k) * a0 + k * a1) / 5;
            table[6] = 0;
            table[7] = MaxChannel;
        }
        return table;
    }

    private static (int R, int G, int B)[] ColourTable(ushort end0, ushort end1)
    {
        var a = Expand565(end0);
        var b = Expand565(end1);
        return
        [
            a, b,
            ((2 * a.R + b.R) / 3, (2 * a.G + b.G) / 3, (2 * a.B + b.B) / 3),
            ((a.R + 2 * b.R) / 3, (a.G + 2 * b.G) / 3, (a.B + 2 * b.B) / 3),
        ];
    }

    private static (int R, int G, int B) Expand565(int value) => (
        (value >> Rgb565RedShift) * MaxChannel / Rgb565BlueMask,
        ((value >> Rgb565GreenShift) & Rgb565GreenMask) * MaxChannel / Rgb565GreenMask,
        (value & Rgb565BlueMask) * MaxChannel / Rgb565BlueMask);

    /// <summary>The 0x2B format keeps luma in the alpha channel, Cr in red, Cb in blue and the real alpha in green (BT.601,
    /// full range); each result is clamped and cut to a whole number.</summary>
    private static void WriteYCbCr(byte[] bgra, int at, int luma, int cr, int alpha, int cb)
    {
        bgra[at] = Channel(luma + BlueFromCb * cb);
        bgra[at + 1] = Channel(luma - GreenFromCb * cb - GreenFromCr * cr);
        bgra[at + 2] = Channel(luma + RedFromCr * cr);
        bgra[at + 3] = (byte)alpha;
    }

    private static byte Channel(float value) => (byte)Math.Clamp(value, 0, MaxChannel);
}
