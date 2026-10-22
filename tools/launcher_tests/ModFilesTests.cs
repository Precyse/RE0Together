using CoopLauncher;

namespace LauncherTests;

/// <summary>The pure rules of a mod's file list: folder expansion, the manifest diff and the status over many files.</summary>
public static class ModFilesTests
{
    public static void Run()
    {
        ExpandsAFolderInStableOrder();
        MissingOrEmptyFolderStaysOneEntry();
        ProfileJsonFolderFlag();
        ManifestRoundTripAndDiff();
        StatusOverManyFiles();
    }

    private static void ExpandsAFolderInStableOrder()
    {
        var root = Path.Combine(Path.GetTempPath(), "cf_expand_" + Guid.NewGuid().ToString("N"));
        try
        {
            Directory.CreateDirectory(Path.Combine(root, "pkg", "scripts", "sub"));
            File.WriteAllText(Path.Combine(root, "pkg", "scripts", "b.lua"), "b");
            File.WriteAllText(Path.Combine(root, "pkg", "scripts", "A.lua"), "a");
            File.WriteAllText(Path.Combine(root, "pkg", "scripts", "sub", "c.lua"), "c");
            var entries = new List<AdapterFile>
            {
                new("pkg/main.dll", "main.dll"),
                new("pkg\\scripts\\", "coop\\scripts", Folder: true),
                new("pkg/last.dll", "last.dll"),
            };

            var expanded = ModFiles.Expand(entries, root);
            Check.That(expanded.Select(f => f.Dst).SequenceEqual(
                ["main.dll", "coop/scripts/A.lua", "coop/scripts/b.lua", "coop/scripts/sub/c.lua", "last.dll"]),
                "a folder entry becomes one entry per file, recursive, in ordinal order, between its neighbours");
            Check.That(expanded[1].Src == "pkg/scripts/A.lua" && expanded[1].FolderDst == "coop/scripts" && !expanded[1].Folder,
                "an expanded entry has the package path, the folder it came from and no folder flag");
            Check.That(expanded[0].FolderDst == null, "a single file keeps no folder");
            Check.That(ModFiles.Expand(entries, root).Select(f => f.Dst).SequenceEqual(expanded.Select(f => f.Dst)), "the order is stable");
        }
        finally
        {
            Directory.Delete(root, recursive: true);
        }
    }

    private static void MissingOrEmptyFolderStaysOneEntry()
    {
        var root = Path.Combine(Path.GetTempPath(), "cf_expand_" + Guid.NewGuid().ToString("N"));
        try
        {
            Directory.CreateDirectory(Path.Combine(root, "empty"));
            var expanded = ModFiles.Expand([new AdapterFile("none", "coop/a", Folder: true), new AdapterFile("empty", "coop/b", Folder: true)], root);
            Check.That(expanded.Count == 2 && expanded.All(f => !f.Folder && f.FolderDst == null),
                "a missing or empty folder stays as one entry naming the folder, so the package reads as incomplete");
        }
        finally
        {
            Directory.Delete(root, recursive: true);
        }
    }

    private static void ProfileJsonFolderFlag()
    {
        var root = Path.Combine(Path.GetTempPath(), "cf_expand_" + Guid.NewGuid().ToString("N"));
        try
        {
            Directory.CreateDirectory(Path.Combine(root, "adapters", "t", "scripts"));
            File.WriteAllText(Path.Combine(root, "adapters", "t", "scripts", "one.lua"), "1");
            const string json = """
                { "id": "t", "name": "T", "steamAppId": 1, "exe": "t.exe", "maxPlayers": 2, "port": 1, "adapterIni": "",
                  "adapterFiles": [ { "src": "adapters/t/scripts", "dst": "coop/scripts", "folder": true } ] }
                """;
            var profile = GameProfile.Parse("t", json, root);
            Check.That(profile.AdapterFiles.Count == 1 && profile.AdapterFiles[0].Dst == "coop/scripts/one.lua", "the profile load expands a folder entry");
        }
        finally
        {
            Directory.Delete(root, recursive: true);
        }
    }

    private static void ManifestRoundTripAndDiff()
    {
        var text = ModFiles.FormatManifest(["coop/scripts/b.lua", "coop/scripts/a.lua"]);
        Check.That(text == "coop/scripts/a.lua\ncoop/scripts/b.lua\n", "the manifest lists the files sorted, one per line");
        Check.That(ModFiles.ParseManifest(text).SequenceEqual(["coop/scripts/a.lua", "coop/scripts/b.lua"]), "the manifest reads back");
        Check.That(ModFiles.ParseManifest(null).Count == 0 && ModFiles.ParseManifest(" \r\n\n").Count == 0, "no manifest or a blank one lists nothing");

        var removed = ModFiles.Removed(["coop/scripts/a.lua", "coop/scripts/old.lua", "coop/scripts/B.lua"], ["coop/scripts/a.lua", "coop/scripts/b.lua"], "coop/scripts");
        Check.That(removed.SequenceEqual(["coop/scripts/old.lua"]), "a file the package dropped is removed; a name differing only by case is kept");
        Check.That(ModFiles.Removed(["coop/scripts/a.lua"], ["coop/scripts/a.lua"], "coop/scripts").Count == 0, "an unchanged set drops nothing");
        Check.That(ModFiles.Removed(["dinput8.dll", "coop/scripts/../../Game.exe", "coop/other/x.lua"], [], "coop/scripts").Count == 0,
            "a damaged manifest cannot point outside its folder");
    }

    private static void StatusOverManyFiles()
    {
        var good = new FileFacts(true, false, false);
        FileFacts[] many(int count, FileFacts one, int at) => Enumerable.Range(0, count).Select(i => i == at ? one : good).ToArray();

        Check.That(ModFiles.Decide([], false) == ModState.NotInstalled, "no files: not installed");
        Check.That(ModFiles.Decide(many(50, good, 0), false) == ModState.Installed, "50 matching files: installed");
        Check.That(ModFiles.Decide(many(50, new FileFacts(false, false, false), 49), false) == ModState.NotInstalled, "one missing file anywhere: not installed");
        Check.That(ModFiles.Decide(many(50, new FileFacts(true, false, true), 25), false) == ModState.UpdateAvailable, "one changed package copy: update available");
        Check.That(ModFiles.Decide(many(50, good, 0), true) == ModState.UpdateAvailable, "a dropped file still installed: update available");
        Check.That(ModFiles.Decide(many(50, new FileFacts(true, true, false), 7), false) == ModState.Modified, "one modified file: modified");
        Check.That(ModFiles.Decide(many(50, new FileFacts(false, false, false, Added: true), 3), false) == ModState.UpdateAvailable,
            "a new file in the package, the rest installed: update available");
        Check.That(ModFiles.Decide(Enumerable.Repeat(new FileFacts(false, false, false, Added: true), 50).ToArray(), false) == ModState.NotInstalled,
            "every file new (a first install): not installed");
        var addedAndMissing = many(50, new FileFacts(false, false, false, Added: true), 3);
        addedAndMissing[9] = new FileFacts(false, false, false);
        Check.That(ModFiles.Decide(addedAndMissing, false) == ModState.NotInstalled, "a new file does not hide an installed file that went missing");
        var both = many(50, new FileFacts(true, true, false), 7);
        both[30] = new FileFacts(true, false, true);
        Check.That(ModFiles.Decide(both, true) == ModState.Modified, "modified outranks update available");
    }
}
