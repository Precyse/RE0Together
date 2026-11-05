using CoopLauncher;

namespace LauncherTests;

/// <summary>SaveBackup: a copy with its manifest, the newest sessions kept, and a restore that refuses a running game or a changed copy.</summary>
public static class SaveBackupTests
{
    private const int KeepSessions = 2;

    public static void Run()
    {
        var root = Path.Combine(Path.GetTempPath(), "cf_backup_" + Guid.NewGuid().ToString("N"));
        var saves = Path.Combine(root, "save");
        var backups = Path.Combine(root, "backups");
        try
        {
            Directory.CreateDirectory(Path.Combine(saves, "slot0"));
            File.WriteAllText(Path.Combine(saves, "slot0", "save.save"), "slot zero");
            File.WriteAllText(Path.Combine(saves, "steam_autocloud.vdf"), "cloud");

            Check.That(SaveBackup.Take(Path.Combine(root, "none"), backups, DateTime.Now, SaveBackup.Before, KeepSessions) == null,
                "no save folder: no backup");
            var start = new DateTime(2026, 11, 5, 10, 0, 0);
            var first = SaveBackup.Take(saves, backups, start, SaveBackup.Before, KeepSessions)!;
            Check.That(Path.GetFileName(first) == "20261105-100000-before", "the folder is named by the stamp and the tag");
            Check.That(File.ReadAllText(Path.Combine(first, "slot0", "save.save")) == "slot zero", "files are copied with their folders");
            var manifest = File.ReadAllLines(Path.Combine(first, SaveBackup.ManifestName));
            Check.That(manifest.Length == 2 && manifest.All(line => line.Split('\t').Length == 3), "the manifest lists every file with size and hash");

            for (var minute = 1; minute <= 5; ++minute) SaveBackup.Take(saves, backups, start.AddMinutes(minute), SaveBackup.After, KeepSessions);
            var kept = SaveBackup.List(backups);
            Check.That(kept.Count == KeepSessions * 2 && Path.GetFileName(kept[0]) == "20261105-100500-after",
                "only the newest sessions stay, newest first");

            File.WriteAllText(Path.Combine(saves, "slot0", "save.save"), "a bad session");
            Check.That(SaveBackup.Restore(kept[0], saves, gameRunning: true) == SaveBackup.RestoreResult.GameRunning,
                "restore refuses while the game runs");
            Check.That(SaveBackup.Restore(kept[0], saves, gameRunning: false) == SaveBackup.RestoreResult.Restored &&
                       File.ReadAllText(Path.Combine(saves, "slot0", "save.save")) == "slot zero", "restore writes the copy back");

            File.WriteAllText(Path.Combine(kept[1], "slot0", "save.save"), "tampered");
            File.WriteAllText(Path.Combine(saves, "slot0", "save.save"), "current");
            Check.That(SaveBackup.Restore(kept[1], saves, gameRunning: false) == SaveBackup.RestoreResult.ManifestMismatch &&
                       File.ReadAllText(Path.Combine(saves, "slot0", "save.save")) == "current", "a changed copy is refused and nothing is written");
        }
        finally
        {
            if (Directory.Exists(root)) Directory.Delete(root, recursive: true);
        }
    }
}
