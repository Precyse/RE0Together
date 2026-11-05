namespace CoopLauncher;

/// <summary>Per-session copies of a game's own save folder, for games where each player plays their own save in the shared
/// session (Crimson Desert): one copy before the game starts and one when the session ends, so a bad session can be compared
/// or undone. Profile key <c>saveBackup</c>: <c>dir</c> the save folder template (<see cref="SavePaths"/>), <c>into</c> the
/// backup folder relative to the game folder, <c>keep</c> how many sessions stay. Each copy is
/// <c>&lt;into&gt;\&lt;yyyyMMdd-HHmmss&gt;-&lt;before|after&gt;\</c> with <see cref="ManifestName"/> listing every file's size and SHA-256.</summary>
public static class SaveBackup
{
    public const string ManifestName = "manifest.tsv";
    public const string Before = "before";
    public const string After = "after";
    private const string StampFormat = "yyyyMMdd-HHmmss";
    private const int CopiesPerSession = 2;

    public enum RestoreResult { Restored, GameRunning, ManifestMismatch, Missing }

    /// <summary>Copies every file under <paramref name="saveDir"/> into a new stamped folder under <paramref name="backupRoot"/>,
    /// writes the manifest, then keeps only the newest <paramref name="keepSessions"/> sessions. Returns the new folder, or null
    /// when the save folder does not exist.</summary>
    public static string? Take(string saveDir, string backupRoot, DateTime now, string tag, int keepSessions)
    {
        if (!Directory.Exists(saveDir)) return null;
        var target = Path.Combine(backupRoot, $"{now.ToString(StampFormat)}-{tag}");
        Directory.CreateDirectory(target);
        var manifest = new List<string>();
        foreach (var file in Directory.EnumerateFiles(saveDir, "*", SearchOption.AllDirectories).Order(StringComparer.Ordinal))
        {
            var relative = Path.GetRelativePath(saveDir, file);
            var copy = Path.Combine(target, relative);
            Directory.CreateDirectory(Path.GetDirectoryName(copy)!);
            File.Copy(file, copy, overwrite: true);
            manifest.Add($"{relative}\t{new FileInfo(copy).Length}\t{ComponentUpdate.Sha256Of(copy)}");
        }
        File.WriteAllLines(Path.Combine(target, ManifestName), manifest);
        Prune(backupRoot, keepSessions * CopiesPerSession);
        return target;
    }

    /// <summary>Writes a backup back into the save folder: refused while the game runs, and refused (nothing written) when any file
    /// in the backup no longer matches its manifest.</summary>
    public static RestoreResult Restore(string backupDir, string saveDir, bool gameRunning)
    {
        if (gameRunning) return RestoreResult.GameRunning;
        var manifestPath = Path.Combine(backupDir, ManifestName);
        if (!File.Exists(manifestPath)) return RestoreResult.Missing;
        var entries = File.ReadAllLines(manifestPath).Select(line => line.Split('\t')).ToList();
        foreach (var entry in entries)
        {
            var copy = Path.Combine(backupDir, entry[0]);
            if (!File.Exists(copy) || ComponentUpdate.Sha256Of(copy) != entry[2]) return RestoreResult.ManifestMismatch;
        }
        foreach (var entry in entries)
        {
            var destination = Path.Combine(saveDir, entry[0]);
            Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
            File.Copy(Path.Combine(backupDir, entry[0]), destination, overwrite: true);
        }
        return RestoreResult.Restored;
    }

    /// <summary>The backup folders, newest first (the stamp sorts by time).</summary>
    public static IReadOnlyList<string> List(string backupRoot) =>
        Directory.Exists(backupRoot)
            ? Directory.GetDirectories(backupRoot).Where(dir => File.Exists(Path.Combine(dir, ManifestName)))
                .OrderByDescending(dir => Path.GetFileName(dir), StringComparer.Ordinal).ToList()
            : [];

    private static void Prune(string backupRoot, int keepFolders)
    {
        foreach (var old in List(backupRoot).Skip(keepFolders)) Directory.Delete(old, recursive: true);
    }
}
