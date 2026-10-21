using System.Collections.Concurrent;
using System.Text.RegularExpressions;
using Microsoft.Win32;

namespace CoopLauncher;

/// <summary>What the launcher reads from a game's appmanifest: its folder name under steamapps\common and its Steam build id.</summary>
public sealed record ManifestInfo(string? InstallDir, int? BuildId);

/// <summary>Finds an installed game's folder through Steam's libraryfolders.vdf and appmanifests. Steam rewrites these files while it
/// updates: a file that cannot be read is skipped, and a parsed file is reused until its write time changes.</summary>
public static class SteamLibrary
{
    private const string DefaultSteamPath = @"C:\Program Files (x86)\Steam";
    private const string EscapedBackslash = @"\\";
    private static readonly Regex PathEntry = new("\"path\"\\s+\"([^\"]+)\"", RegexOptions.Compiled);
    private static readonly Regex BuildIdEntry = new("\"buildid\"\\s+\"(\\d+)\"", RegexOptions.Compiled);
    private static readonly Regex InstallDirEntry = new("\"installdir\"\\s+\"([^\"]+)\"", RegexOptions.Compiled);

    private sealed record Cached<T>(DateTime WrittenUtc, T Value);

    private static readonly ConcurrentDictionary<string, Cached<IReadOnlyList<string>>> LibraryFiles = new();
    private static readonly ConcurrentDictionary<string, Cached<ManifestInfo>> ManifestFiles = new();
    private static readonly ConcurrentDictionary<string, bool> Unreadable = new();

    public static string? FindGameDir(int appId)
    {
        foreach (var (steamApps, manifest) in Manifests(appId))
        {
            if (ReadManifest(manifest)?.InstallDir is not { } installDir) continue;
            var dir = Path.Combine(steamApps, "common", installDir);
            if (Directory.Exists(dir)) return dir;
        }
        return null;
    }

    /// <summary>The Steam build id of the installed game (the appmanifest's buildid): from the manifest beside the given game
    /// folder (steamapps\common\game, so a folder chosen in the settings reports its own build), else from any Steam
    /// library. Null when there is no manifest.</summary>
    public static int? InstalledBuild(int appId, string? gameDir)
    {
        var beside = gameDir == null ? null : Path.GetFullPath(Path.Combine(gameDir, "..", "..", $"appmanifest_{appId}.acf"));
        var manifests = (beside != null && File.Exists(beside) ? new[] { beside } : Array.Empty<string>())
            .Concat(Manifests(appId).Select(m => m.Manifest));
        foreach (var manifest in manifests)
            if (ReadManifest(manifest)?.BuildId is { } build) return build;
        return null;
    }

    /// <summary>Every library folder libraryfolders.vdf lists, with the escaped backslashes (\\) turned back into one.</summary>
    public static IReadOnlyList<string> ParseLibraryPaths(string vdfText) =>
        PathEntry.Matches(vdfText).Select(match => Unescape(match.Groups[1].Value)).ToList();

    /// <summary>The install folder name and build id of an appmanifest; a key the text lacks is null.</summary>
    public static ManifestInfo ParseManifest(string text)
    {
        var installDir = InstallDirEntry.Match(text);
        var buildId = BuildIdEntry.Match(text);
        return new ManifestInfo(
            installDir.Success ? Unescape(installDir.Groups[1].Value) : null,
            buildId.Success && int.TryParse(buildId.Groups[1].Value, out var build) ? build : null);
    }

    /// <summary>The parsed appmanifest, or null when the file cannot be read (Steam is writing it, or it is gone).</summary>
    public static ManifestInfo? ReadManifest(string path) => ReadCached(ManifestFiles, path, ParseManifest);

    /// <summary>The library folders of a libraryfolders.vdf, none when the file cannot be read.</summary>
    public static IReadOnlyList<string> ReadLibraryPaths(string vdfPath) => ReadCached(LibraryFiles, vdfPath, ParseLibraryPaths) ?? [];

    /// <summary>Steam cloud cache of a game for one account: Steam\userdata\accountId\appId\remote.</summary>
    public static string UserRemoteDir(int appId, uint accountId) =>
        Path.Combine(SteamRoot(), "userdata", accountId.ToString(), appId.ToString(), "remote");

    /// <summary>Steam's local artwork cache for a game: capsule, hero and logo images, some in hashed subfolders.</summary>
    public static string LibraryCacheDir(int appId) =>
        Path.Combine(SteamRoot(), "appcache", "librarycache", appId.ToString());

    /// <summary>Custom library artwork the player set in Steam (right-click the game's art, "Set custom ..."): every
    /// account's userdata\id\config\grid file named appId plus the suffix, newest first.</summary>
    public static IEnumerable<string> CustomArtFiles(int appId, string suffix)
    {
        var userdata = Path.Combine(SteamRoot(), "userdata");
        if (!Directory.Exists(userdata)) return [];
        return Directory.EnumerateDirectories(userdata)
            .Select(account => Path.Combine(account, "config", "grid"))
            .Where(Directory.Exists)
            .SelectMany(grid => GridImageExtensions.Select(extension => Path.Combine(grid, $"{appId}{suffix}{extension}")))
            .Where(File.Exists)
            .OrderByDescending(File.GetLastWriteTimeUtc);
    }

    private static readonly string[] GridImageExtensions = { ".png", ".jpg", ".jpeg", ".webp" };

    private static string SteamRoot() =>
        (Registry.CurrentUser.OpenSubKey(@"Software\Valve\Steam")?.GetValue("SteamPath") as string ?? DefaultSteamPath)
        .Replace('/', '\\');

    private static string Unescape(string value) => value.Replace(EscapedBackslash, @"\");

    private static IEnumerable<(string SteamApps, string Manifest)> Manifests(int appId)
    {
        foreach (var library in LibraryPaths())
        {
            var steamApps = Path.Combine(library, "steamapps");
            var manifest = Path.Combine(steamApps, $"appmanifest_{appId}.acf");
            if (File.Exists(manifest)) yield return (steamApps, manifest);
        }
    }

    private static IEnumerable<string> LibraryPaths()
    {
        var steamPath = SteamRoot();
        yield return steamPath;
        var vdf = Path.Combine(steamPath, "steamapps", "libraryfolders.vdf");
        if (!File.Exists(vdf)) yield break;
        foreach (var library in ReadLibraryPaths(vdf)) yield return library;
    }

    /// <summary>The parsed file, reread only when its write time changed; null when it cannot be read.</summary>
    private static T? ReadCached<T>(ConcurrentDictionary<string, Cached<T>> cache, string path, Func<string, T> parse)
    {
        try
        {
            var written = File.GetLastWriteTimeUtc(path);
            if (cache.TryGetValue(path, out var cached) && cached.WrittenUtc == written) return cached.Value;
            var value = parse(ReadText(path));
            cache[path] = new Cached<T>(written, value);
            Unreadable.TryRemove(path, out _);
            return value;
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            if (Unreadable.TryAdd(path, true)) Log.Info($"Could not read {path}, skipping it: {e.Message}");
            return default;
        }
    }

    /// <summary>Reads the file even while Steam has it open for writing.</summary>
    private static string ReadText(string path)
    {
        using var reader = new StreamReader(new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete));
        return reader.ReadToEnd();
    }
}
