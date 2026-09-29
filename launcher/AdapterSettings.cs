namespace CoopLauncher;

/// <summary>The launcher's part of the adapter config: the coop flag in the adapter ini and the session save folder.</summary>
public static class AdapterSettings
{
    private const string CoopKey = "coop";

    public static void EnableCoop(string gameDir, SaveSyncProfile config) =>
        SetCoop(Path.Combine(gameDir, config.AdapterIni), enabled: true);

    /// <summary>Sets coop=0 and deletes the session folder, so a normal launch plays vanilla.</summary>
    public static void Reset(string gameDir, SaveSyncProfile config)
    {
        var ini = Path.Combine(gameDir, config.AdapterIni);
        var sessionDir = Path.Combine(gameDir, config.SessionDir);
        if (File.Exists(ini) && ReadLines(ini).Any(IsCoopEnabled)) SetCoop(ini, enabled: false);
        if (!Directory.Exists(sessionDir)) return;
        Directory.Delete(sessionDir, recursive: true);
        Log.Info($"Removed session folder {sessionDir}");
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
