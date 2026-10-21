using CoopLauncher;

namespace LauncherTests;

/// <summary>AdapterSettings.Reset on temp folders: the archive skips an unchanged set, never collides, is bounded by count and age, and
/// a failed copy keeps the session folder.</summary>
public static class AdapterSettingsTests
{
    private const string SessionDir = "coop/session";
    private const string SaveName = "data0.bin";

    private static readonly SaveSyncProfile Config = new([SaveName], SessionDir);
    private static readonly GameProfile Profile = new("t", "Test", 1, "t.exe", 2, 1, [], "coop/adapter.ini");

    public static void Run()
    {
        ExpiredArchivesKeepNewestCountAndAge();
        var game = Path.Combine(Path.GetTempPath(), "cf_adapter_" + Guid.NewGuid().ToString("N"));
        try
        {
            ArchivesSessionWithoutStagingAndTemp(Path.Combine(game, "first"));
            UnchangedSetIsNotArchivedAgain(Path.Combine(game, "dedupe"));
            SameSecondResetsNeverCollide(Path.Combine(game, "stamp"));
            CountRetentionEndToEnd(Path.Combine(game, "count"));
            FailedCopyKeepsTheSessionFolder(Path.Combine(game, "locked"));
        }
        finally
        {
            Directory.Delete(game, recursive: true);
        }
    }

    private static string Session(string game) => Path.Combine(game, "coop", "session");

    private static string ArchiveRoot(string game) => Path.Combine(game, "coop", AdapterSettings.ArchiveFolder);

    private static int Archives(string game) => Directory.Exists(ArchiveRoot(game)) ? Directory.GetDirectories(ArchiveRoot(game)).Length : 0;

    private static void WriteSession(string game, string text)
    {
        Directory.CreateDirectory(Session(game));
        File.WriteAllText(Path.Combine(Session(game), SaveName), text);
    }

    private static void ExpiredArchivesKeepNewestCountAndAge()
    {
        var now = new DateTime(2026, 2, 1, 12, 0, 0);
        var old = new[] { "20260101-000000", "20250101-000000", "20240101-000000" };
        Check.That(AdapterSettings.ExpiredArchives(old, now).SequenceEqual(["20250101-000000", "20240101-000000"]),
            "archives older than 30 days go, the newest stays even when it is older");

        var recent = Enumerable.Range(0, AdapterSettings.MaxArchives + 3).Select(i => now.AddMinutes(-i).ToString(AppData.StampFormat)).ToList();
        var expired = AdapterSettings.ExpiredArchives(recent, now);
        Check.That(expired.Count == 3 && expired.All(name => string.CompareOrdinal(name, recent[AdapterSettings.MaxArchives - 1]) < 0),
            "only the oldest archives beyond the count limit go");

        Check.That(AdapterSettings.ExpiredArchives(["20260201-115900", "20260201-115900-2", "20260201-115900-3"], now).Count == 0,
            "suffixed archives of the same second count as separate archives and are kept");
        Check.That(AdapterSettings.ExpiredArchives(["20260201-115900", "mine"], now).Count == 0, "a folder that is not a timestamp never expires by age");
    }

    private static void ArchivesSessionWithoutStagingAndTemp(string game)
    {
        WriteSession(game, "progress");
        Directory.CreateDirectory(Path.Combine(Session(game), SaveReceiver.StagingFolder));
        File.WriteAllText(Path.Combine(Session(game), SaveReceiver.StagingFolder, SaveName), "staged");
        File.WriteAllText(Path.Combine(Session(game), SaveName + SaveReceiver.TempSuffix), "half");
        Directory.CreateDirectory(Path.Combine(game, "coop"));
        File.WriteAllLines(Path.Combine(game, "coop", "adapter.ini"), ["coop=1"]);

        AdapterSettings.Reset(game, Profile, Config);

        Check.That(!Directory.Exists(Session(game)), "the session folder is removed");
        var archive = Directory.GetDirectories(ArchiveRoot(game)).Single();
        Check.That(Directory.GetFiles(archive, "*", SearchOption.AllDirectories).Select(Path.GetFileName).SequenceEqual([SaveName]), "the archive holds the save, not staging or temp files");
        Check.That(File.ReadAllText(Path.Combine(archive, SaveName)) == "progress", "the archived save is the session save");
        Check.That(File.ReadAllText(Path.Combine(game, "coop", "adapter.ini")).Contains("coop=0"), "coop is switched off");
    }

    private static void UnchangedSetIsNotArchivedAgain(string game)
    {
        WriteSession(game, "same");
        AdapterSettings.Reset(game, Profile, Config);
        WriteSession(game, "same");
        AdapterSettings.Reset(game, Profile, Config);
        Check.That(Archives(game) == 1, "a set equal to the newest archive is not archived again");
        Check.That(!Directory.Exists(Session(game)), "the unchanged session folder is still removed");

        WriteSession(game, "changed");
        AdapterSettings.Reset(game, Profile, Config);
        Check.That(Archives(game) == 2, "a changed set is archived");
    }

    private static void SameSecondResetsNeverCollide(string game)
    {
        for (var i = 0; i < 4; i++)
        {
            WriteSession(game, "set " + i);
            AdapterSettings.Reset(game, Profile, Config);
        }
        Check.That(Archives(game) == 4 && !Directory.Exists(Session(game)), "four resets in the same second make four archives and remove the session each time");
    }

    private static void CountRetentionEndToEnd(string game)
    {
        var now = DateTime.Now;
        for (var i = 1; i <= AdapterSettings.MaxArchives + 2; i++)
        {
            var archive = Path.Combine(ArchiveRoot(game), now.AddMinutes(-i).ToString(AppData.StampFormat));
            Directory.CreateDirectory(archive);
            File.WriteAllText(Path.Combine(archive, SaveName), "old " + i);
        }
        WriteSession(game, "newest");
        AdapterSettings.Reset(game, Profile, Config);
        Check.That(Archives(game) == AdapterSettings.MaxArchives, "the archive keeps at most the limit");
    }

    private static void FailedCopyKeepsTheSessionFolder(string game)
    {
        WriteSession(game, "keep me");
        var save = Path.Combine(Session(game), SaveName);
        var threw = false;
        using (new FileStream(save, FileMode.Open, FileAccess.Read, FileShare.None))
        {
            try
            {
                AdapterSettings.Reset(game, Profile, Config);
            }
            catch (IOException)
            {
                threw = true;
            }
        }
        Check.That(!threw, "a save that cannot be read does not throw out of Reset");
        Check.That(File.ReadAllText(save) == "keep me", "the session folder is kept when its saves could not be archived");
        Check.That(Archives(game) == 0, "no partial archive is left");
    }
}
