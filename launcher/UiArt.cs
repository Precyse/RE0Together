using System.Drawing.Imaging;

namespace CoopLauncher.Gui;

/// <summary>The game art the in-game overlay draws, taken from the player's own install into <c>coop\ui</c> when the game is
/// launched (nothing of the game is kept in the launcher's repository): the message font's atlas and layout, the three parts of
/// the dialog panel and the picture of every outfit that has one. Each part is written again only when the archive it came from
/// changed (the keys are in <see cref="StampFile"/>). The game's files are only read; the file names are the adapter's
/// (adapters/re0/src/ui_art_files.h).</summary>
internal static class UiArt
{
    private const string FormatVersion = "1";
    private const string StampFile = "art.key";
    private const string TempExtension = ".tmp";
    private const string OutfitFolder = "outfits";
    private const string OutfitExtension = ".png";
    private const string FontPart = "font";
    private const string DialogPart = "dialog";
    private const string OutfitPartPrefix = "outfit/";
    private const char StampSeparator = '=';
    private const string FontImage = "font_eng.png";
    private const string FontLayout = "font_eng.gfd";

    /// <summary>The dialog panel's parts in the texture (x, y, width, height): the leather, the top rod and the bottom rod.</summary>
    private static readonly (string File, int[] Rect)[] DialogParts =
    [
        ("dialog_leather.png", [0, 0, 720, 395]),
        ("dialog_rod_top.png", [0, 397, 746, 46]),
        ("dialog_rod_bottom.png", [0, 446, 746, 48]),
    ];

    /// <summary>Folder the overlay reads the art from.</summary>
    public static string FolderOf(string gameDir) => Path.Combine(gameDir, "coop", "ui");

    /// <summary>Brings <c>coop\ui</c> up to date; a part that cannot be read is logged and left out (the overlay then draws without it).</summary>
    public static void Refresh(GameProfile game, string gameDir)
    {
        if (game.UiArt is not { } source) return;
        var folder = FolderOf(gameDir);
        Directory.CreateDirectory(Path.Combine(folder, OutfitFolder));
        var old = ParseStamp(ReadStamp(folder));
        var current = new Dictionary<string, string>();
        foreach (var part in Parts(game, source, gameDir))
        {
            var done = old.TryGetValue(part.Name, out var key) && key == part.Key && part.Files.All(file => File.Exists(Path.Combine(folder, file)));
            try
            {
                if (!done) part.Write(folder);
                current[part.Name] = part.Key;
            }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException or InvalidDataException or ArgumentException)
            {
                Log.Info($"UI art {part.Name} unreadable: {e.Message}");
            }
        }
        File.WriteAllText(Path.Combine(folder, StampFile), FormatStamp(current));
    }

    /// <summary>Stamp lines <c>part=key</c>, ordered by part.</summary>
    public static string FormatStamp(IReadOnlyDictionary<string, string> keys) =>
        string.Concat(keys.OrderBy(pair => pair.Key, StringComparer.Ordinal).Select(pair => $"{pair.Key}{StampSeparator}{pair.Value}\n"));

    /// <summary>The keys of a stamp; a line without a separator is skipped.</summary>
    public static Dictionary<string, string> ParseStamp(string text)
    {
        var keys = new Dictionary<string, string>();
        foreach (var line in text.Split('\n', StringSplitOptions.RemoveEmptyEntries))
        {
            var at = line.IndexOf(StampSeparator);
            if (at > 0) keys[line[..at]] = line[(at + 1)..].TrimEnd('\r');
        }
        return keys;
    }

    private sealed record Part(string Name, string Key, string[] Files, Action<string> Write);

    private static IEnumerable<Part> Parts(GameProfile game, UiArtSource source, string gameDir)
    {
        var fontArc = Path.Combine(gameDir, source.FontArc.Replace('/', Path.DirectorySeparatorChar));
        if (File.Exists(fontArc))
            yield return new Part(FontPart, OutfitArt.ArchiveKey(gameDir, fontArc, $"{source.FontTexture}|{source.FontGfd}|{FormatVersion}").ToString("x8"),
                [FontImage, FontLayout], folder => WriteFont(fontArc, source, folder));
        var dialogArc = Path.Combine(gameDir, source.DialogArc.Replace('/', Path.DirectorySeparatorChar));
        if (File.Exists(dialogArc))
            yield return new Part(DialogPart, OutfitArt.ArchiveKey(gameDir, dialogArc, $"{source.DialogTexture}|{FormatVersion}").ToString("x8"),
                DialogParts.Select(part => part.File).ToArray(), folder => WriteDialog(dialogArc, source, folder));
        foreach (var option in game.Options?.Where(candidate => candidate.Character != null) ?? [])
            foreach (var value in option.AvailableIn(gameDir))
            {
                if (OutfitArt.CachePath(game, gameDir, value) is not { } cache) continue;
                var file = Path.Combine(OutfitFolder, value.Value + OutfitExtension);
                yield return new Part(OutfitPartPrefix + value.Value, Path.GetFileName(cache), [file], folder => WriteOutfit(game, gameDir, value, cache, Path.Combine(folder, file)));
            }
    }

    private static void WriteFont(string arc, UiArtSource source, string folder)
    {
        var texture = Entry(arc, source.FontTexture);
        var layout = Entry(arc, source.FontGfd);
        WritePng(MtTexture.Decode(texture), Path.Combine(folder, FontImage));
        WriteFile(Path.Combine(folder, FontLayout), layout);
    }

    private static void WriteDialog(string arc, UiArtSource source, string folder)
    {
        var texture = MtTexture.Decode(Entry(arc, source.DialogTexture));
        foreach (var (file, rect) in DialogParts) WritePng(OutfitArt.Crop(texture, rect), Path.Combine(folder, file));
    }

    private static void WriteOutfit(GameProfile game, string gameDir, OptionValue value, string cache, string target)
    {
        OutfitArt.Load(game, gameDir, value)?.Dispose();
        WriteFile(target, File.ReadAllBytes(cache));
    }

    private static byte[] Entry(string arc, string name) =>
        MtArc.ReadEntry(arc, name) ?? throw new InvalidDataException($"{Path.GetFileName(arc)} has no {name}");

    private static void WritePng(DecodedTexture picture, string path)
    {
        using var bitmap = AvatarPixels.ToBitmap(picture.Bgra, picture.Width, picture.Height);
        using var stream = new MemoryStream();
        bitmap.Save(stream, ImageFormat.Png);
        WriteFile(path, stream.ToArray());
    }

    /// <summary>Written beside the target and moved into place, so a game reading the folder never sees half a file.</summary>
    private static void WriteFile(string path, byte[] bytes)
    {
        var temp = path + TempExtension;
        File.WriteAllBytes(temp, bytes);
        File.Move(temp, path, overwrite: true);
    }

    private static string ReadStamp(string folder)
    {
        var path = Path.Combine(folder, StampFile);
        return File.Exists(path) ? File.ReadAllText(path) : string.Empty;
    }
}
