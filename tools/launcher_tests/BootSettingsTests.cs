using CoopLauncher;

namespace LauncherTests;

/// <summary>Boot keys: a running game may get a session only when the ini it started with (the file, unwritten since the process started)
/// holds the values the session writes.</summary>
public static class BootSettingsTests
{
    private static readonly string[] BootKeys = ["coop", "role"];
    private static readonly DateTime Started = new(2026, 1, 1, 12, 0, 0, DateTimeKind.Utc);
    private static readonly GameProfile Profile = new("t", "Test", 1, "t.exe", 2, 1, [], "coop/adapter.ini");

    public static void Run()
    {
        DecidesFromTheIniAndTheProcessStart();
        AnUnchangedIniKeepsItsWriteTime();
    }

    private static Dictionary<string, string> Keys(string coop, string role) => new() { ["coop"] = coop, ["role"] = role };

    private static void DecidesFromTheIniAndTheProcessStart()
    {
        var before = Started.AddMinutes(-1);
        var written = Keys("1", "host");
        Check.That(!BootSettings.Differ(BootKeys, written, Keys("1", "host"), before, Started), "boot: the same values are allowed");
        Check.That(BootSettings.Differ(BootKeys, written, Keys("1", "guest"), before, Started), "boot: another value of a boot key is refused");
        Check.That(BootSettings.Differ(BootKeys, written, Keys("0", "host"), before, Started), "boot: a session after a reset (coop=0) is refused");
        Check.That(BootSettings.Differ(BootKeys, written, new Dictionary<string, string> { ["coop"] = "1" }, before, Started), "boot: a missing key reads as empty");
        Check.That(BootSettings.Differ(BootKeys, written, Keys("1", "host"), Started.AddSeconds(1), Started), "boot: an ini written after the game started is not known, refused");
        Check.That(BootSettings.Differ(BootKeys, written, null, null, Started), "boot: no ini is not known, refused");
        Check.That(BootSettings.Differ(BootKeys, written, Keys("1", "host"), before, null), "boot: a start time that cannot be read is not known, refused");
        Check.That(!BootSettings.Differ(BootKeys, new Dictionary<string, string> { ["other"] = "1" }, Keys("0", "x"), Started.AddSeconds(1), Started),
            "boot: keys the session does not write cannot differ");
        Check.That(!BootSettings.Differ(["trace"], written, Keys("0", "x"), Started.AddSeconds(1), Started), "boot: a key that is not a boot key is not looked at");
        Check.That(!BootSettings.Blocks(Profile, "unused", written), "boot: a profile without boot keys never blocks");
    }

    private static void AnUnchangedIniKeepsItsWriteTime()
    {
        var game = Path.Combine(Path.GetTempPath(), "cf_boot_" + Guid.NewGuid().ToString("N"));
        try
        {
            AdapterSettings.WriteOptions(game, Profile, Keys("1", "host"));
            var ini = Path.Combine(game, Profile.AdapterIni);
            var old = Started.AddDays(-1);
            File.SetLastWriteTimeUtc(ini, old);
            AdapterSettings.WriteOptions(game, Profile, Keys("1", "host"));
            Check.That(File.GetLastWriteTimeUtc(ini) == old, "boot: writing the values the ini holds leaves the file alone");
            AdapterSettings.WriteOptions(game, Profile, Keys("1", "guest"));
            Check.That(File.GetLastWriteTimeUtc(ini) != old, "boot: a changed value is written");
            var read = AdapterSettings.ReadKeys(game, Profile);
            Check.That(read?["role"] == "guest" && read["coop"] == "1" && AdapterSettings.ReadKeys(game + "x", Profile) == null, "boot: the ini's keys are read as they are");
        }
        finally
        {
            if (Directory.Exists(game)) Directory.Delete(game, recursive: true);
        }
    }
}
