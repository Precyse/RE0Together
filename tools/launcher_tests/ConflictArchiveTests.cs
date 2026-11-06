using CoopLauncher;

namespace LauncherTests;

/// <summary>ConflictArchive: a conflicting mod's files and data folder (its saves) move into one stamped archive folder under the
/// same paths; nothing moves while the game runs or when the mod is not there; the dd2.json conflict names DD2gether.</summary>
public static class ConflictArchiveTests
{
    public static void Run()
    {
        var conflict = new ConflictProfile("DD2gether", ["mods/coop", "mods/coop.lua", "data/dd2gether", "data/absent.json"], "coop/archive");
        var game = new GameProfile("test", "Test", 1, "test.exe", 2, 1, [], "coop/adapter.ini", Conflicts: [conflict]);
        var now = new DateTime(2026, 11, 5, 18, 39, 11);

        WithFolder(dir =>
        {
            Directory.CreateDirectory(Path.Combine(dir, "mods", "coop"));
            File.WriteAllText(Path.Combine(dir, "mods", "coop", "init.lua"), "script");
            File.WriteAllText(Path.Combine(dir, "mods", "coop.lua"), "loader");
            Directory.CreateDirectory(Path.Combine(dir, "data", "dd2gether", "profiles"));
            File.WriteAllText(Path.Combine(dir, "data", "dd2gether", "profiles", "data000.bin"), "save");
            File.WriteAllText(Path.Combine(dir, "mods", "other.lua"), "another mod");

            Check.That(ConflictArchive.Present(conflict, dir).Count == 3, "the conflict's present paths are found");
            Check.That(ConflictArchive.Run(game, dir, gameRunning: true, now).Count == 0 && Directory.Exists(Path.Combine(dir, "mods", "coop")),
                "nothing moves while the game runs");
            var made = ConflictArchive.Run(game, dir, gameRunning: false, now);
            var archive = Path.Combine(dir, "coop", "archive", "dd2gether_20261105_183911");
            Check.That(made.SequenceEqual([archive]) && ConflictArchive.Present(conflict, dir).Count == 0, "every path moves into one stamped archive");
            Check.That(File.ReadAllText(Path.Combine(archive, "mods", "coop", "init.lua")) == "script" &&
                       File.ReadAllText(Path.Combine(archive, "data", "dd2gether", "profiles", "data000.bin")) == "save",
                "scripts and saves keep their paths inside the archive");
            Check.That(File.Exists(Path.Combine(dir, "mods", "other.lua")), "another mod's files stay");
            Check.That(ConflictArchive.Run(game, dir, gameRunning: false, now).Count == 0, "with the mod gone nothing more is archived");
        });
        var dd2gether = GameProfile.Load("dd2").Conflicts?.SingleOrDefault();
        Check.That(dd2gether is { Name: "DD2gether", Into: "coop/archive" } && dd2gether.Paths.Contains("reframework/data/dd2gether"),
            "dd2.json archives DD2gether with its data folder");
    }

    private static void WithFolder(Action<string> body)
    {
        var dir = Path.Combine(Path.GetTempPath(), "cf_conflict_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(dir);
        try
        {
            body(dir);
        }
        finally
        {
            Directory.Delete(dir, recursive: true);
        }
    }
}
