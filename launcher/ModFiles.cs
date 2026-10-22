namespace CoopLauncher;

/// <summary>What the status of one mod file shows: both the file and its ownership marker are there, the file differs from its
/// marker (another mod or damage), the package copy differs from the file, and for a missing file whether it is new in the
/// package (an earlier install never put it there, so the mod needs an update, not a repair).</summary>
public readonly record struct FileFacts(bool Present, bool Modified, bool Stale, bool Added = false);

/// <summary>One file of a mod on disk. Bookkeeping (marker, backups, staging, the build record) sits at <see cref="Side"/>: the
/// file itself for a single adapter file, a mirror under the folder's <c>.cfmod</c> for a file of a folder entry, so a folder the
/// game scans (SR2 hashes every file of <c>coop\scripts</c>) holds nothing but the mod's own files.</summary>
internal readonly record struct ModFile(AdapterFile Entry, string Src, string Dst, string Side)
{
    public string Marker => Side + ".cfown";
    public string Backup => Side + BackupSuffix;
    public string Temp => Side + ".tmp";
    public string Previous => Side + ".prev";
    public string Build => Side + ".cfbuild";

    public const string BackupSuffix = ".cfbak";
}

/// <summary>The pure rules of a mod's file list: folder entries expanded to one entry per file, where bookkeeping goes, the
/// manifest of the files a folder holds, and the status of a whole mod from its files.</summary>
public static class ModFiles
{
    private const char Separator = '/';
    private const string BookkeepingFolder = ".cfmod";
    private const string ManifestName = ".cfmanifest";

    /// <summary>The entries with every folder entry replaced by its files, recursively, in ordinal order of their path inside the
    /// folder (the order is the same on every machine, the mod hash depends on it). A folder entry is resolved against
    /// <paramref name="root"/>; one that is missing or empty stays as a single entry that names the folder, so installing
    /// reports the package as incomplete.</summary>
    public static List<AdapterFile> Expand(IEnumerable<AdapterFile> entries, string root) =>
        entries.SelectMany(entry => entry.Folder ? ExpandFolder(entry, root) : [entry]).ToList();

    private static IEnumerable<AdapterFile> ExpandFolder(AdapterFile folder, string root)
    {
        var source = Path.Combine(root, folder.Src);
        var names = !Directory.Exists(source) ? []
            : Directory.EnumerateFiles(source, "*", SearchOption.AllDirectories)
                .Select(path => Path.GetRelativePath(source, path).Replace('\\', Separator))
                .Order(StringComparer.Ordinal).ToList();
        if (names.Count == 0) return [folder with { Folder = false }];
        var src = Normalized(folder.Src);
        var dst = Normalized(folder.Dst);
        return names.Select(name => new AdapterFile(src + Separator + name, dst + Separator + name, FolderDst: dst));
    }

    private static string Normalized(string path) => path.Replace('\\', Separator).TrimEnd(Separator);

    /// <summary>Where the bookkeeping of a folder entry lives: <c>.cfmod/&lt;folder name&gt;</c> beside the folder.</summary>
    public static string BookkeepingDir(string gameDir, string folderDst) =>
        Path.Combine(gameDir, Path.GetDirectoryName(folderDst) ?? "", BookkeepingFolder, Path.GetFileName(folderDst));

    /// <summary>The path bookkeeping of this file is built on (see <see cref="ModFile"/>).</summary>
    internal static string SidePath(string gameDir, AdapterFile entry) =>
        entry.FolderDst is { } folder
            ? Path.Combine(BookkeepingDir(gameDir, folder), entry.Dst[(folder.Length + 1)..])
            : Path.Combine(gameDir, entry.Dst);

    /// <summary>The file listing the files a folder entry owns (installed there, destinations inside the game folder).</summary>
    public static string ManifestPath(string gameDir, string folderDst) => Path.Combine(BookkeepingDir(gameDir, folderDst), ManifestName);

    public static string FormatManifest(IEnumerable<string> dsts) =>
        string.Concat(dsts.Order(StringComparer.Ordinal).Select(dst => dst + "\n"));

    public static List<string> ParseManifest(string? text) =>
        (text ?? "").Split('\n', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries).ToList();

    /// <summary>The files of the previous manifest that the current file list no longer has (a renamed or dropped file). Only a
    /// path inside the folder counts, so a damaged manifest can never point at another file.</summary>
    public static List<string> Removed(IEnumerable<string> previous, IReadOnlyCollection<string> current, string folderDst) =>
        previous.Where(dst => InFolder(dst, folderDst) && !current.Contains(dst, StringComparer.OrdinalIgnoreCase)).ToList();

    private static bool InFolder(string dst, string folderDst) =>
        dst.StartsWith(folderDst + Separator, StringComparison.OrdinalIgnoreCase) && !dst.Split(Separator).Contains("..");

    /// <summary>Installed when every file is there and matches its marker; modified when one does not match; an update is available
    /// when a package copy differs, the package has a new file or a file it dropped is still installed; not installed when nothing
    /// is there or a file that an earlier install put there is missing.</summary>
    public static ModState Decide(IReadOnlyCollection<FileFacts> files, bool droppedFileInstalled)
    {
        if (files.Count == 0 || files.All(file => !file.Present) || files.Any(file => !file.Present && !file.Added)) return ModState.NotInstalled;
        if (files.Any(file => file.Modified)) return ModState.Modified;
        return files.Any(file => file.Stale || file.Added) || droppedFileInstalled ? ModState.UpdateAvailable : ModState.Installed;
    }
}
