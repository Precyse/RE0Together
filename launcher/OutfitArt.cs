using System.Drawing.Drawing2D;
using System.Drawing.Imaging;

namespace CoopLauncher.Gui;

/// <summary>The pictures of a game's outfits, taken from the game's own files when the launcher runs (nothing of the game is
/// kept in the launcher's repository): where an outfit's picture is (the profile's outfitArt pattern, a DLC outfit's folder,
/// a value's own art), how it is extracted (cutouts are trimmed to the figure, other pictures cropped and feathered) and the
/// PNG cache in <see cref="AppData.ArtDir"/>. The game's files are only read.</summary>
internal static class OutfitArt
{
    private const int ArtFormatVersion = 1;
    private const string ValueToken = "{value}";
    private const string DlcFolder = "DLC";
    private const string DlcCostumeFolders = "costume_*";
    private const string CacheExtension = ".png";
    private const string TempExtension = ".tmp";
    private const byte TrimAlpha = 8;
    private const int TrimMargin = 4;
    private const int CropFields = 4;
    private const int BytesPerPixel = 4;
    private const int AlphaOffset = 3;

    /// <summary>The archive that holds the outfit's picture: the game folder's own, or for a DLC outfit the first
    /// <c>DLC\app\costume_*</c> folder that has it; null when the game does not have the outfit.</summary>
    public static string? Locate(string gameDir, ArtSource source, OptionValue value)
    {
        var relative = source.Arc.Replace(ValueToken, value.Value).Replace('/', Path.DirectorySeparatorChar);
        if (value.DlcAppId == null)
        {
            var own = Path.Combine(gameDir, relative);
            return File.Exists(own) ? own : null;
        }
        var root = Path.Combine(gameDir, DlcFolder, value.DlcAppId);
        return Directory.Exists(root)
            ? Directory.GetDirectories(root, DlcCostumeFolders).Order(StringComparer.Ordinal).Select(folder => Path.Combine(folder, relative)).FirstOrDefault(File.Exists)
            : null;
    }

    /// <summary>The picture of an outfit, from the cache or else extracted from the game and cached; null when the value has
    /// no picture (the saved outfit, a game without art, a DLC outfit that is not installed). A picture that cannot be read
    /// throws <see cref="IOException"/>, <see cref="UnauthorizedAccessException"/>, <see cref="InvalidDataException"/> or
    /// <see cref="ArgumentException"/>.</summary>
    public static Bitmap? Load(GameProfile game, string gameDir, OptionValue value)
    {
        if (value.Value.Length == 0 || (value.Art ?? game.OutfitArt) is not { } source || Locate(gameDir, source, value) is not { } arc) return null;
        var texture = source.Texture.Replace(ValueToken, value.Value);
        var cached = Path.Combine(AppData.ArtDir, game.Id, $"{value.Value}-{CacheKey(gameDir, arc, source, texture):x8}{CacheExtension}");
        if (ReadCached(cached) is { } hit) return hit;

        var body = MtArc.ReadEntry(arc, texture) ?? throw new InvalidDataException($"{Path.GetFileName(arc)} has no {texture}");
        var picture = Shape(MtTexture.Decode(body), source);
        var bitmap = AvatarPixels.ToBitmap(picture.Bgra, picture.Width, picture.Height);
        Store(bitmap, cached, value.Value);
        return bitmap;
    }

    /// <summary>The picture's shape in one colour: its transparency filled with <paramref name="fill"/>.</summary>
    public static Bitmap Silhouette(Bitmap picture, Color fill)
    {
        var silhouette = new Bitmap(picture.Width, picture.Height, PixelFormat.Format32bppArgb);
        var matrix = new ColorMatrix(new[]
        {
            new float[] { 0, 0, 0, 0, 0 },
            new float[] { 0, 0, 0, 0, 0 },
            new float[] { 0, 0, 0, 0, 0 },
            new float[] { 0, 0, 0, 1, 0 },
            new[] { fill.R / 255f, fill.G / 255f, fill.B / 255f, 0, 1 },
        });
        using var attributes = new ImageAttributes();
        attributes.SetColorMatrix(matrix);
        using var g = Graphics.FromImage(silhouette);
        g.DrawImage(picture, new Rectangle(0, 0, picture.Width, picture.Height), 0, 0, picture.Width, picture.Height, GraphicsUnit.Pixel, attributes);
        return silhouette;
    }

    /// <summary>A cutout is trimmed to its figure's columns (the full height stays, so every outfit of a character paints at
    /// one scale); a picture with a crop is cut to it and its edges fade out.</summary>
    private static DecodedTexture Shape(DecodedTexture texture, ArtSource source)
    {
        if (source.Crop == null) return Trim(texture);
        var cut = Crop(texture, source.Crop);
        return source.Feather > 0 ? Feather(cut, source.Feather) : cut;
    }

    private static DecodedTexture Trim(DecodedTexture texture)
    {
        var first = texture.Width;
        var last = -1;
        for (var x = 0; x < texture.Width; x++)
        {
            for (var y = 0; y < texture.Height; y++)
            {
                if (texture.Bgra[(y * texture.Width + x) * BytesPerPixel + AlphaOffset] <= TrimAlpha) continue;
                first = Math.Min(first, x);
                last = x;
                break;
            }
        }
        if (last < 0) return texture;
        var left = Math.Max(0, first - TrimMargin);
        return Cut(texture, left, 0, Math.Min(texture.Width, last + 1 + TrimMargin) - left, texture.Height);
    }

    private static DecodedTexture Crop(DecodedTexture texture, IReadOnlyList<int> crop)
    {
        if (crop.Count != CropFields || crop[0] < 0 || crop[1] < 0 || crop[2] <= 0 || crop[3] <= 0
            || crop[0] + crop[2] > texture.Width || crop[1] + crop[3] > texture.Height)
            throw new InvalidDataException($"the crop does not fit a {texture.Width}x{texture.Height} picture");
        return Cut(texture, crop[0], crop[1], crop[2], crop[3]);
    }

    private static DecodedTexture Cut(DecodedTexture texture, int x, int y, int width, int height)
    {
        var bgra = new byte[width * height * BytesPerPixel];
        for (var row = 0; row < height; row++)
            Buffer.BlockCopy(texture.Bgra, ((y + row) * texture.Width + x) * BytesPerPixel, bgra, row * width * BytesPerPixel, width * BytesPerPixel);
        return new DecodedTexture(width, height, bgra);
    }

    /// <summary>Fades an opaque picture to transparent over <paramref name="band"/> pixels at its left, right and bottom edges
    /// (a linear ramp, the smallest of the three).</summary>
    private static DecodedTexture Feather(DecodedTexture texture, int band)
    {
        var bgra = (byte[])texture.Bgra.Clone();
        for (var y = 0; y < texture.Height; y++)
        {
            var fromBottom = Ramp(texture.Height - 1 - y, band);
            for (var x = 0; x < texture.Width; x++)
            {
                var factor = Math.Min(Math.Min(Ramp(x, band), Ramp(texture.Width - 1 - x, band)), fromBottom);
                var at = (y * texture.Width + x) * BytesPerPixel + AlphaOffset;
                bgra[at] = (byte)(bgra[at] * factor);
            }
        }
        return new DecodedTexture(texture.Width, texture.Height, bgra);
    }

    private static float Ramp(int distance, int band) => Math.Clamp((float)distance / band, 0, 1);

    /// <summary>The cache file's name part: the archive's place, size and time with what is taken from it, so a changed game
    /// file or a changed way of taking the picture makes a new file. FNV-1a, 32 bit.</summary>
    private static uint CacheKey(string gameDir, string arc, ArtSource source, string texture)
    {
        var info = new FileInfo(arc);
        var crop = source.Crop == null ? string.Empty : string.Join(',', source.Crop);
        var text = $"{Path.GetRelativePath(gameDir, arc)}|{info.Length}|{info.LastWriteTimeUtc.Ticks}|{texture}|{crop}|{source.Feather}|{ArtFormatVersion}";
        return Fnv1a.Hash(System.Text.Encoding.UTF8.GetBytes(text));
    }

    /// <summary>The cached picture, or null when there is none or the file is not a picture (it is then made again).</summary>
    private static Bitmap? ReadCached(string path)
    {
        if (!File.Exists(path)) return null;
        try
        {
            using var stream = new MemoryStream(File.ReadAllBytes(path));
            using var loaded = new Bitmap(stream);
            return new Bitmap(loaded);
        }
        catch (ArgumentException)
        {
            return null;
        }
    }

    /// <summary>Writes the picture beside its cache name and moves it into place, then removes the outfit's older files.</summary>
    private static void Store(Bitmap bitmap, string path, string value)
    {
        var folder = Path.GetDirectoryName(path)!;
        Directory.CreateDirectory(folder);
        var temp = path + TempExtension;
        bitmap.Save(temp, ImageFormat.Png);
        File.Move(temp, path, overwrite: true);
        foreach (var old in Directory.GetFiles(folder, $"{value}-*{CacheExtension}").Where(file => file != path)) File.Delete(old);
    }
}
