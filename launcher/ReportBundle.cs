using System.IO.Compression;
using System.Runtime.InteropServices;
using System.Text;

namespace CoopLauncher;

/// <summary>One zip to send when something goes wrong: report.txt (launcher build, OS, each game's folder, build and mod state, what
/// was left out and why), the launcher's settings and every log it kept, and from each game's coop folder the adapter log and ini, the
/// game's status file, the logs and crash dumps of the other player's machine, the crash files its guests sent (<c>coop\peer_crash</c>),
/// and this machine's crash dumps (<c>coop\dumps</c> and <c>crash-*.dmp</c>). Never the join file
/// (it holds the session password). One file that cannot be read is listed in report.txt and skipped; each log is cut to its last
/// <see cref="MaxLogEntryBytes"/>, every other file (a crash dump cannot be read without its start) goes whole.</summary>
public static class ReportBundle
{
    public const int MaxLogEntryBytes = 8 * 1024 * 1024;

    private const string FilePrefix = "report-";
    private const string ManifestName = "report.txt";
    private const string SettingsEntryName = "settings.json";
    private const string LogExtension = ".log";
    private const string LogPattern = "*" + LogExtension;
    private static readonly string[] GameFilePatterns = { "*adapter.log", "*adapter.ini", "peer_*", "crash-*.dmp" };
    private static readonly string[] CoopSubfolders = { "dumps", CrashRules.PeerFolder };

    /// <summary>Writes the zip next to the launcher log and returns its path; null when it could not be written.</summary>
    public static string? Create(IEnumerable<GameProfile> games)
    {
        var path = Path.Combine(AppData.LogsDir, $"{FilePrefix}{DateTime.Now.ToString(AppData.StampFormat)}.zip");
        try
        {
            Directory.CreateDirectory(AppData.LogsDir);
            using (var zip = ZipFile.Open(path, ZipArchiveMode.Create))
            {
                var entries = new Entries(zip);
                AddLauncherFiles(entries);
                var gameLines = games.SelectMany(game => AddGame(entries, game)).ToList();
                entries.WriteManifest(LauncherLines().Concat(gameLines));
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

    private static IEnumerable<string> LauncherLines()
    {
        var build = BuildCheck.LocalBuild();
        yield return $"Launcher build: {(build == BuildInfo.DevBuild ? "dev build" : build.ToString())}";
        yield return $"OS: {RuntimeInformation.OSDescription}";
        yield return $"Created: {DateTime.Now:yyyy-MM-dd HH:mm:ss}";
    }

    private static void AddLauncherFiles(Entries entries)
    {
        foreach (var log in entries.Files(AppData.LogsDir, LogPattern)) entries.Add(log, Path.GetFileName(log));
        entries.Add(AppData.SettingsFile, SettingsEntryName);
        entries.Add(InstalledRelease.PathFor(AppContext.BaseDirectory), InstalledRelease.FileName);
    }

    /// <summary>Adds the game's files and returns its lines for report.txt.</summary>
    private static List<string> AddGame(Entries entries, GameProfile game)
    {
        var gameDir = GameFolders.Find(game);
        var lines = new List<string> { "", $"Game {game.Id} ({game.Name})", $"  folder: {gameDir ?? "not found"}" };
        if (gameDir == null) return lines;
        var mod = ModInstaller.Status(game, gameDir);
        var build = GameBuilds.Installed(game, gameDir);
        lines.Add($"  game build: {(build == GameBuilds.Unknown ? "unknown" : build.ToString())}");
        lines.Add($"  mod: {mod.State}{(mod.Build == 0 ? "" : $", build {mod.Build}")}");
        if (game.AdapterLog != null)
        {
            var coopDir = Path.GetDirectoryName(Path.Combine(gameDir, game.AdapterLog))!;
            foreach (var pattern in GameFilePatterns)
                foreach (var file in entries.Files(coopDir, pattern).Where(file => !IsJoinFile(game, file)))
                    entries.Add(file, $"{game.Id}/{Path.GetFileName(file)}");
            foreach (var folder in CoopSubfolders)
                foreach (var file in entries.Files(Path.Combine(coopDir, folder), "*", SearchOption.AllDirectories))
                    entries.Add(file, $"{game.Id}/{folder}/{Path.GetRelativePath(Path.Combine(coopDir, folder), file).Replace('\\', '/')}");
        }
        if (game.StatusFile != null) entries.Add(Path.Combine(gameDir, game.StatusFile), $"{game.Id}/{Path.GetFileName(game.StatusFile)}");
        return lines;
    }

    private static bool IsJoinFile(GameProfile game, string file) =>
        game.JoinFile != null && Path.GetFileName(file).Equals(Path.GetFileName(game.JoinFile), StringComparison.OrdinalIgnoreCase);

    /// <summary>The zip's entries: each file is read without locking it, a log cut to its last <see cref="MaxLogEntryBytes"/>,
    /// and recorded as included, truncated or skipped for report.txt.</summary>
    private sealed class Entries(ZipArchive zip)
    {
        private readonly HashSet<string> _names = new(StringComparer.OrdinalIgnoreCase);
        private readonly List<string> _included = [];
        private readonly List<string> _truncated = [];
        private readonly List<string> _skipped = [];

        /// <summary>The files of a folder that match the pattern, with the folders under it if asked (none for a missing folder); a folder that cannot be listed is recorded as skipped.</summary>
        public IEnumerable<string> Files(string directory, string pattern, SearchOption depth = SearchOption.TopDirectoryOnly)
        {
            if (!Directory.Exists(directory)) return [];
            try
            {
                return Directory.GetFiles(directory, pattern, depth);
            }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException)
            {
                _skipped.Add($"{Path.Combine(directory, pattern)}: {e.Message}");
                return [];
            }
        }

        /// <summary>Adds the file as it is now; a missing file is not an error, a file that cannot be read is listed as skipped.</summary>
        public void Add(string file, string entryName)
        {
            if (!File.Exists(file) || !_names.Add(entryName)) return;
            ZipArchiveEntry? entry = null;
            try
            {
                using var source = new FileStream(file, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
                var cut = entryName.EndsWith(LogExtension, StringComparison.OrdinalIgnoreCase) ? Math.Max(0, source.Length - MaxLogEntryBytes) : 0;
                source.Seek(cut, SeekOrigin.Begin);
                entry = zip.CreateEntry(entryName, CompressionLevel.Optimal);
                using (var target = entry.Open()) source.CopyTo(target);
                _included.Add(entryName);
                if (cut > 0) _truncated.Add($"{entryName}: first {cut} bytes left out");
            }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException)
            {
                entry?.Delete();
                _skipped.Add($"{entryName}: {e.Message}");
            }
        }

        public void WriteManifest(IEnumerable<string> lines)
        {
            var text = new StringBuilder();
            foreach (var line in lines) text.AppendLine(line);
            AppendSection(text, "Included", _included);
            AppendSection(text, "Truncated", _truncated);
            AppendSection(text, "Skipped", _skipped);
            using var writer = new StreamWriter(zip.CreateEntry(ManifestName, CompressionLevel.Optimal).Open(), new UTF8Encoding(false));
            writer.Write(text.ToString());
        }

        private static void AppendSection(StringBuilder text, string title, List<string> items)
        {
            text.AppendLine().AppendLine($"{title} ({items.Count})");
            foreach (var item in items) text.AppendLine($"  {item}");
        }
    }
}
