using System.Globalization;
using System.Security.Cryptography;

namespace CoopLauncher;

/// <summary>The launcher's part of the adapter config: the coop flag and the game's options in the adapter ini, and the
/// session save folder.</summary>
public static class AdapterSettings
{
    public const string ArchiveFolder = "archive";  // next to the session folder
    public const int MaxArchives = 20;
    public const int KeptArchiveDays = 30;

    private const string CoopKey = "coop";

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
    /// copied to an archive folder first, so the guest's progress is never lost: a set identical to the newest archive is not
    /// archived again, at most <see cref="MaxArchives"/> archives are kept, none older than <see cref="KeptArchiveDays"/> days
    /// but the newest. A session folder that could not be archived is kept.</summary>
    public static void Reset(string gameDir, GameProfile profile, SaveSyncProfile config)
    {
        var ini = IniPath(gameDir, profile);
        var sessionDir = Path.Combine(gameDir, config.SessionDir);
        if (File.Exists(ini) && ReadLines(ini).Any(IsCoopEnabled)) SetKeys(ini, new Dictionary<string, string> { [CoopKey] = "0" });
        if (!Directory.Exists(sessionDir)) return;
        if (!ArchiveSession(sessionDir))
        {
            Log.Info($"Kept session folder {sessionDir}: its saves could not be archived");
            return;
        }
        Directory.Delete(sessionDir, recursive: true);
        Log.Info($"Removed session folder {sessionDir}");
    }

    /// <summary>The archives to delete, given every archive folder name (a timestamp, optionally with a -2, -3 suffix): the
    /// newest is always kept, then at most <see cref="MaxArchives"/> in all, none older than <see cref="KeptArchiveDays"/> days.
    /// A name that is not a timestamp never expires by age.</summary>
    public static List<string> ExpiredArchives(IEnumerable<string> names, DateTime now)
    {
        var cutoff = now.AddDays(-KeptArchiveDays);
        return names.OrderDescending(StringComparer.Ordinal)
            .Select((name, index) => (Name: name, Index: index))
            .Where(entry => entry.Index > 0 && (entry.Index >= MaxArchives || StampTime(entry.Name) < cutoff))
            .Select(entry => entry.Name)
            .ToList();
    }

    /// <summary>True when the session's saves are safely in the archive (copied, or already there unchanged), or there were none.</summary>
    private static bool ArchiveSession(string sessionDir)
    {
        string? target = null;
        try
        {
            var files = Directory.EnumerateFiles(sessionDir, "*", SearchOption.AllDirectories)
                .Select(path => Path.GetRelativePath(sessionDir, path))
                .Where(relative => !relative.StartsWith(SaveReceiver.StagingFolder + Path.DirectorySeparatorChar) && !relative.EndsWith(SaveReceiver.TempSuffix))
                .ToList();
            if (files.Count == 0) return true;
            var archiveRoot = Path.Combine(Path.GetDirectoryName(sessionDir)!, ArchiveFolder);
            var newest = Directory.Exists(archiveRoot) ? Directory.GetDirectories(archiveRoot).OrderDescending(StringComparer.Ordinal).FirstOrDefault() : null;
            if (newest != null && files.All(relative => SameContent(Path.Combine(sessionDir, relative), Path.Combine(newest, relative))))
            {
                Log.Info($"Session saves unchanged since the archive {Path.GetFileName(newest)}, not archived again");
                return true;
            }
            target = UniqueTarget(archiveRoot, DateTime.Now.ToString(AppData.StampFormat));
            foreach (var relative in files)
            {
                var destination = Path.Combine(target, relative);
                Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
                File.Copy(Path.Combine(sessionDir, relative), destination);
            }
            Log.Info($"Archived {files.Count} session files to {target}");
            foreach (var expired in ExpiredArchives(Directory.GetDirectories(archiveRoot).Select(Path.GetFileName).OfType<string>(), DateTime.Now))
                DeleteQuietly(Path.Combine(archiveRoot, expired));
            return true;
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            Log.Info($"Could not archive the session saves: {e.Message}");
            if (target != null) DeleteQuietly(target);
            return false;
        }
    }

    /// <summary>The archive folder for this timestamp; a second one in the same second gets -2, -3, ...</summary>
    private static string UniqueTarget(string archiveRoot, string stamp)
    {
        var target = Path.Combine(archiveRoot, stamp);
        for (var number = 2; Directory.Exists(target); number++) target = Path.Combine(archiveRoot, $"{stamp}-{number}");
        return target;
    }

    private static DateTime? StampTime(string archiveName) =>
        archiveName.Length >= AppData.StampFormat.Length
        && DateTime.TryParseExact(archiveName[..AppData.StampFormat.Length], AppData.StampFormat, CultureInfo.InvariantCulture, DateTimeStyles.None, out var time)
            ? time
            : null;

    private static bool SameContent(string path, string archived)
    {
        if (!File.Exists(archived)) return false;
        return new FileInfo(path).Length == new FileInfo(archived).Length && Hash(path).AsSpan().SequenceEqual(Hash(archived));
    }

    private static byte[] Hash(string path)
    {
        using var stream = File.OpenRead(path);
        return SHA256.HashData(stream);
    }

    private static void DeleteQuietly(string directory)
    {
        try
        {
            if (Directory.Exists(directory)) Directory.Delete(directory, recursive: true);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            Log.Info($"Could not delete {directory}: {e.Message}");
        }
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
