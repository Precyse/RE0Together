using System.IO.Compression;

namespace CoopLauncher;

/// <summary>One zip to send when something goes wrong: the launcher's logs, its build, and from each game's coop folder
/// the adapter log and ini, the logs and crash dumps of the other player's machine, and this machine's crash dumps.</summary>
public static class ReportBundle
{
    private const string FilePrefix = "report-";
    private const string TimestampFormat = "yyyyMMdd-HHmmss";
    private static readonly string[] GameFilePatterns = { "adapter.log", "adapter.ini", "peer_*", "crash-*.dmp" };

    /// <summary>Writes the zip next to the launcher log and returns its path; null when it could not be written.</summary>
    public static string? Create(IEnumerable<GameProfile> games)
    {
        var path = Path.Combine(AppData.LogsDir, $"{FilePrefix}{DateTime.Now.ToString(TimestampFormat)}.zip");
        try
        {
            Directory.CreateDirectory(AppData.LogsDir);
            using (var zip = ZipFile.Open(path, ZipArchiveMode.Create))
            {
                AddFile(zip, AppData.LogFile, Path.GetFileName(AppData.LogFile));
                AddFile(zip, AppData.PreviousLogFile, Path.GetFileName(AppData.PreviousLogFile));
                AddFile(zip, Path.Combine(AppContext.BaseDirectory, BuildCheck.VersionFile), BuildCheck.VersionFile);
                foreach (var game in games) AddGame(zip, game);
            }
            Log.Info($"Report written: {path}");
            return path;
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            Log.Info($"Could not write the report: {e.Message}");
            return null;
        }
    }

    private static void AddGame(ZipArchive zip, GameProfile game)
    {
        if (game.AdapterLog == null || GameFolders.Find(game) is not { } gameDir) return;
        var coopDir = Path.GetDirectoryName(Path.Combine(gameDir, game.AdapterLog))!;
        if (!Directory.Exists(coopDir)) return;
        foreach (var pattern in GameFilePatterns)
            foreach (var file in Directory.EnumerateFiles(coopDir, pattern))
                AddFile(zip, file, $"{game.Id}/{Path.GetFileName(file)}");
    }

    /// <summary>Adds the file as it is now; a file the game still has open is read without locking it.</summary>
    private static void AddFile(ZipArchive zip, string file, string entryName)
    {
        if (!File.Exists(file)) return;
        using var source = new FileStream(file, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
        using var entry = zip.CreateEntry(entryName, CompressionLevel.Optimal).Open();
        source.CopyTo(entry);
    }
}
