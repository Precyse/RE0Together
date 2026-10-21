using System.IO.Compression;
using CoopLauncher;

namespace LauncherTests;

/// <summary>ReportBundle on a temp data folder and a temp game folder: the manifest, the status file, no join file, the size cap on logs (dumps go whole), and a locked file skipped and listed.</summary>
public static class ReportBundleTests
{
    private const string GameId = "cf_report_game";
    private const string JoinPassword = "s3cret-pw";

    public static void Run()
    {
        using var data = new TempData();
        var game = Path.Combine(data.Root, "game");
        var coop = Path.Combine(game, "coop");
        Directory.CreateDirectory(coop);
        Directory.CreateDirectory(AppData.LogsDir);

        File.WriteAllText(AppData.LogFile, "launcher log");
        File.WriteAllText(AppData.PreviousLogFile, "previous log");
        File.WriteAllText(Path.Combine(AppData.LogsDir, "launcher.3.log"), "older run");
        File.WriteAllText(Path.Combine(coop, "adapter.ini"), "coop=1");
        File.WriteAllText(Path.Combine(coop, "status.txt"), "searching 3");
        File.WriteAllText(Path.Combine(coop, "join.txt"), "password=" + JoinPassword);
        File.WriteAllText(Path.Combine(coop, "peer_76561198000000001_20260101-120000.log"), "peer");
        File.WriteAllText(Path.Combine(coop, "crash-1.dmp"), "dump");
        File.WriteAllText(Path.Combine(coop, "peer_2_locked.log"), "locked");
        var big = new byte[ReportBundle.MaxLogEntryBytes + 100];
        for (var i = 0; i < big.Length; i++) big[i] = (byte)(i % 251);
        File.WriteAllBytes(Path.Combine(coop, "adapter.log"), big);
        File.WriteAllBytes(Path.Combine(coop, "crash-2.dmp"), big);

        var profile = new GameProfile(GameId, "Report Game", 1, "cf_report_not_running.exe", 2, 1, [], "coop/adapter.ini",
            AdapterLog: "coop/adapter.log", JoinFile: "coop/join.txt", StatusFile: "coop/status.txt");
        AppSettings.Update(s => s.WithGameFolder(GameId, game));

        string? zipPath;
        using (new FileStream(Path.Combine(coop, "peer_2_locked.log"), FileMode.Open, FileAccess.Read, FileShare.None))
            zipPath = ReportBundle.Create([profile]);

        Check.That(zipPath != null && File.Exists(zipPath), "the report is written");
        if (zipPath == null) return;
        using var zip = ZipFile.OpenRead(zipPath);
        var names = zip.Entries.Select(entry => entry.FullName).ToList();

        Check.That(new[] { "launcher.log", "launcher.prev.log", "launcher.3.log", "report.txt", "settings.json" }.All(names.Contains),
            "the report holds every launcher log, the settings and the manifest");
        Check.That(new[] { "adapter.ini", "status.txt", "crash-1.dmp", "peer_76561198000000001_20260101-120000.log", "adapter.log" }.All(name => names.Contains($"{GameId}/{name}")),
            "the report holds the game's adapter files, its status file, peer logs and dumps");
        Check.That(!names.Any(name => name.EndsWith("join.txt")), "the join file is never in the report");
        Check.That(zip.Entries.All(entry => !Read(entry).Contains(JoinPassword)), "the session password is nowhere in the report");

        var adapterLog = zip.GetEntry($"{GameId}/adapter.log")!;
        Check.That(adapterLog.Length == ReportBundle.MaxLogEntryBytes, "an entry is cut to the size cap");
        using (var stream = adapterLog.Open())
        {
            var bytes = new byte[ReportBundle.MaxLogEntryBytes];
            stream.ReadExactly(bytes);
            Check.That(bytes.AsSpan().SequenceEqual(big.AsSpan(100)), "the cut keeps the last bytes");
        }

        Check.That(zip.GetEntry($"{GameId}/crash-2.dmp")!.Length == big.Length, "a crash dump is never cut (it cannot be read without its start)");

        Check.That(!names.Contains($"{GameId}/peer_2_locked.log"), "a locked file is not in the report");
        var manifest = Read(zip.GetEntry("report.txt")!);
        Check.That(manifest.Contains("peer_2_locked.log"), "the locked file is listed as skipped");
        Check.That(manifest.Contains("Launcher build:") && manifest.Contains("OS:") && manifest.Contains($"Game {GameId}") && manifest.Contains(game),
            "the manifest names the launcher build, the OS and each game with its folder");
        Check.That(manifest.Contains("mod: NotInstalled") && manifest.Contains("adapter.log: first 100 bytes left out"), "the manifest names the mod state and the truncated entry");
    }

    private static string Read(ZipArchiveEntry entry)
    {
        using var reader = new StreamReader(entry.Open());
        return reader.ReadToEnd();
    }
}
