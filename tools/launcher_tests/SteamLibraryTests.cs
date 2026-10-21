using CoopLauncher;

namespace LauncherTests;

/// <summary>SteamLibrary parsing (escaped backslashes, missing keys) and file reading (a locked file is skipped, a parsed file is reused until it changes).</summary>
public static class SteamLibraryTests
{
    private const string LibraryFolders = """
        "libraryfolders"
        {
        	"0"
        	{
        		"path"		"C:\\Program Files (x86)\\Steam"
        		"apps"
        		{
        			"1"		"2"
        		}
        	}
        	"1"
        	{
        		"path"		"D:\\SteamLibrary"
        	}
        }
        """;

    private const string Manifest = """
        "AppState"
        {
        	"appid"		"1"
        	"installdir"		"Saints Row 2"
        	"buildid"		"17178773"
        }
        """;

    public static void Run()
    {
        ParsesEscapedLibraryPaths();
        ParsesManifestWithMissingKeys();
        var dir = Path.Combine(Path.GetTempPath(), "cf_steam_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(dir);
        try
        {
            LockedManifestIsSkipped(dir);
            ParsedFileIsReusedUntilItChanges(dir);
        }
        finally
        {
            Directory.Delete(dir, recursive: true);
        }
    }

    private static void ParsesEscapedLibraryPaths()
    {
        Check.That(SteamLibrary.ParseLibraryPaths(LibraryFolders).SequenceEqual([@"C:\Program Files (x86)\Steam", @"D:\SteamLibrary"]),
            "library paths come back with single backslashes");
        Check.That(SteamLibrary.ParseLibraryPaths("").Count == 0 && SteamLibrary.ParseLibraryPaths("\"libraryfolders\" { }").Count == 0, "no path key: no libraries");
    }

    private static void ParsesManifestWithMissingKeys()
    {
        var full = SteamLibrary.ParseManifest(Manifest);
        Check.That(full.InstallDir == "Saints Row 2" && full.BuildId == 17178773, "install folder and build id are read");
        Check.That(SteamLibrary.ParseManifest("\"AppState\" { \"installdir\" \"Game\" }") == new ManifestInfo("Game", null), "a missing build id is null");
        Check.That(SteamLibrary.ParseManifest("\"AppState\" { \"buildid\" \"5\" }") == new ManifestInfo(null, 5), "a missing install folder is null");
        Check.That(SteamLibrary.ParseManifest("") == new ManifestInfo(null, null), "an empty manifest has neither");
        Check.That(SteamLibrary.ParseManifest("\"installdir\" \"Odd\\\\Name\"").InstallDir == @"Odd\Name", "an escaped backslash in the install folder is unescaped");
    }

    private static void LockedManifestIsSkipped(string dir)
    {
        var path = Path.Combine(dir, "appmanifest_locked.acf");
        File.WriteAllText(path, Manifest);
        ManifestInfo? whileLocked;
        using (new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.None)) whileLocked = SteamLibrary.ReadManifest(path);
        Check.That(whileLocked == null, "a manifest that cannot be opened is skipped, not thrown");
        Check.That(SteamLibrary.ReadManifest(path)?.BuildId == 17178773, "the same manifest is read once it is free");
        Check.That(SteamLibrary.ReadManifest(Path.Combine(dir, "missing.acf")) == null, "a missing manifest is null");

        using (var writing = new FileStream(path + ".w", FileMode.Create, FileAccess.Write, FileShare.ReadWrite))
        {
            writing.Write(System.Text.Encoding.ASCII.GetBytes(Manifest));
            writing.Flush();
            Check.That(SteamLibrary.ReadManifest(path + ".w")?.InstallDir == "Saints Row 2", "a manifest Steam still has open for writing is read");
        }
    }

    private static void ParsedFileIsReusedUntilItChanges(string dir)
    {
        var path = Path.Combine(dir, "appmanifest_cached.acf");
        var written = new DateTime(2026, 1, 1, 0, 0, 0, DateTimeKind.Utc);
        File.WriteAllText(path, Manifest);
        File.SetLastWriteTimeUtc(path, written);
        Check.That(SteamLibrary.ReadManifest(path)?.BuildId == 17178773, "first read parses the file");

        File.WriteAllText(path, Manifest.Replace("17178773", "5"));
        File.SetLastWriteTimeUtc(path, written);
        Check.That(SteamLibrary.ReadManifest(path)?.BuildId == 17178773, "an unchanged write time reuses the parsed file");

        File.SetLastWriteTimeUtc(path, written.AddMinutes(1));
        Check.That(SteamLibrary.ReadManifest(path)?.BuildId == 5, "a new write time reads the file again");
    }
}
