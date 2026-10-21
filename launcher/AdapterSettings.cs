namespace CoopLauncher;

/// <summary>The launcher's part of the adapter config: the coop flag and the game's options in the adapter ini, and the
/// session save folder.</summary>
public static class AdapterSettings
{
    private const string CoopKey = "coop";
    public const string ArchiveFolder = "archive";  // next to the session folder
    private const string ArchiveStampFormat = "yyyyMMdd-HHmmss";
    private const string TempSuffix = ".part";
    private const int KeptArchives = 5;

    /// <summary>Sets coop=1 and one line per game option (the adapter reads the ini once, at its start).</summary>
    public static void EnableCoop(string gameDir, GameProfile profile, IReadOnlyDictionary<string, string> options)
    {
        var keys = options.ToDictionary(pair => pair.Key, pair => pair.Value);
        keys[CoopKey] = "1";
        SetKeys(IniPath(gameDir, profile), keys);
    }

    /// <summary>Sets one line per game option, for a game whose adapter needs no coop flag.</summary>
    public static void WriteOptions(string gameDir, GameProfile profile, IReadOnlyDictionary<string, string> options) =>
        SetKeys(IniPath(gameDir, profile), options);

    /// <summary>Sets coop=0 and deletes the session folder, so a normal launch plays vanilla. The guest's saves in it are
    /// copied to an archive folder first (the last <see cref="KeptArchives"/> are kept), so the guest's progress is never lost.</summary>
    public static void Reset(string gameDir, GameProfile profile, SaveSyncProfile config)
    {
        var ini = IniPath(gameDir, profile);
        var sessionDir = Path.Combine(gameDir, config.SessionDir);
        if (File.Exists(ini) && ReadLines(ini).Any(IsCoopEnabled)) SetKeys(ini, new Dictionary<string, string> { [CoopKey] = "0" });
        if (!Directory.Exists(sessionDir)) return;
        ArchiveSession(sessionDir);
        Directory.Delete(sessionDir, recursive: true);
        Log.Info($"Removed session folder {sessionDir}");
    }

    private static void ArchiveSession(string sessionDir)
    {
        var files = Directory.EnumerateFiles(sessionDir, "*", SearchOption.AllDirectories)
            .Select(path => Path.GetRelativePath(sessionDir, path))
            .Where(relative => !relative.StartsWith(SaveReceiver.StagingFolder + Path.DirectorySeparatorChar) && !relative.EndsWith(TempSuffix))
            .ToList();
        if (files.Count == 0) return;
        var archiveRoot = Path.Combine(Path.GetDirectoryName(sessionDir)!, ArchiveFolder);
        var target = Path.Combine(archiveRoot, DateTime.Now.ToString(ArchiveStampFormat));
        foreach (var relative in files)
        {
            var destination = Path.Combine(target, relative);
            Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
            File.Copy(Path.Combine(sessionDir, relative), destination);
        }
        Log.Info($"Archived {files.Count} session files to {target}");
        foreach (var old in Directory.GetDirectories(archiveRoot).OrderDescending().Skip(KeptArchives)) Directory.Delete(old, recursive: true);
    }

    private static string IniPath(string gameDir, GameProfile profile) => Path.Combine(gameDir, profile.AdapterIni);

    private static void SetKeys(string iniPath, IReadOnlyDictionary<string, string> keys)
    {
        var lines = File.Exists(iniPath) ? ReadLines(iniPath) : [];
        foreach (var (key, value) in keys)
        {
            var line = $"{key}={value}";
            var index = lines.FindIndex(existing => IsKeyLine(existing, key));
            if (index >= 0) lines[index] = line;
            else lines.Add(line);
        }
        Directory.CreateDirectory(Path.GetDirectoryName(iniPath)!);
        File.WriteAllLines(iniPath, lines);
        Log.Info($"Set {string.Join(", ", keys.Select(pair => $"{pair.Key}={pair.Value}"))} in {iniPath}");
    }

    private static List<string> ReadLines(string path) => File.ReadAllLines(path).ToList();

    private static bool IsKeyLine(string line, string key) =>
        line.Split('=', 2) is [var existing, _] && existing.Trim() == key;

    private static bool IsCoopEnabled(string line) =>
        IsKeyLine(line, CoopKey) && line.Split('=', 2)[1].Trim() == "1";
}
