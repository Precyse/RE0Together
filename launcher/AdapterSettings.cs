namespace CoopLauncher;

/// <summary>The launcher's part of the adapter config: the coop flag in the adapter ini and the session save folder.</summary>
public static class AdapterSettings
{
    private const string CoopKey = "coop";
    private const string ArchiveFolder = "archive";  // next to the session folder
    private const string ArchiveStampFormat = "yyyyMMdd-HHmmss";
    private const string TempSuffix = ".part";
    private const int KeptArchives = 5;

    public static void EnableCoop(string gameDir, SaveSyncProfile config) =>
        SetCoop(Path.Combine(gameDir, config.AdapterIni), enabled: true);

    /// <summary>Sets coop=0 and deletes the session folder, so a normal launch plays vanilla. The guest's saves in it are
    /// copied to an archive folder first (the last <see cref="KeptArchives"/> are kept), so the guest's progress is never lost.</summary>
    public static void Reset(string gameDir, SaveSyncProfile config)
    {
        var ini = Path.Combine(gameDir, config.AdapterIni);
        var sessionDir = Path.Combine(gameDir, config.SessionDir);
        if (File.Exists(ini) && ReadLines(ini).Any(IsCoopEnabled)) SetCoop(ini, enabled: false);
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

    private static void SetCoop(string iniPath, bool enabled)
    {
        var line = $"{CoopKey}={(enabled ? 1 : 0)}";
        var lines = File.Exists(iniPath) ? ReadLines(iniPath) : [];
        var index = lines.FindIndex(IsCoopLine);
        if (index >= 0) lines[index] = line;
        else lines.Add(line);
        Directory.CreateDirectory(Path.GetDirectoryName(iniPath)!);
        File.WriteAllLines(iniPath, lines);
        Log.Info($"Set {line} in {iniPath}");
    }

    private static List<string> ReadLines(string path) => File.ReadAllLines(path).ToList();

    private static bool IsCoopLine(string line) =>
        line.Split('=', 2) is [var key, _] && key.Trim() == CoopKey;

    private static bool IsCoopEnabled(string line) =>
        IsCoopLine(line) && line.Split('=', 2)[1].Trim() == "1";
}
