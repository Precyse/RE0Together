using System.Text.RegularExpressions;
using Microsoft.Win32;

namespace CoopLauncher;

/// <summary>Finds an installed game's folder through Steam's libraryfolders.vdf and appmanifests.</summary>
public static class SteamLibrary
{
    private const string DefaultSteamPath = @"C:\Program Files (x86)\Steam";
    private static readonly Regex PathEntry = new("\"path\"\\s+\"([^\"]+)\"", RegexOptions.Compiled);
    private static readonly Regex BuildIdEntry = new("\"buildid\"\\s+\"(\\d+)\"", RegexOptions.Compiled);
    private static readonly Regex InstallDirEntry = new("\"installdir\"\\s+\"([^\"]+)\"", RegexOptions.Compiled);

    public static string? FindGameDir(int appId)
    {
        foreach (var (steamApps, manifest) in Manifests(appId))
        {
            var match = InstallDirEntry.Match(File.ReadAllText(manifest));
            if (!match.Success) continue;
            var dir = Path.Combine(steamApps, "common", match.Groups[1].Value);
            if (Directory.Exists(dir)) return dir;
        }
        return null;
    }

    /// <summary>The Steam build id of the installed game (the appmanifest's buildid), or null when Steam has no manifest for it.</summary>
    public static int? InstalledBuild(int appId)
    {
        foreach (var (_, manifest) in Manifests(appId))
        {
            var match = BuildIdEntry.Match(File.ReadAllText(manifest));
            if (match.Success && int.TryParse(match.Groups[1].Value, out var build)) return build;
        }
        return null;
    }

    /// <summary>Steam cloud cache of a game for one account: Steam\userdata\accountId\appId\remote.</summary>
    public static string UserRemoteDir(int appId, uint accountId) =>
        Path.Combine(SteamRoot(), "userdata", accountId.ToString(), appId.ToString(), "remote");

    /// <summary>Steam's local artwork cache for a game: capsule, hero and logo images, some in hashed subfolders.</summary>
    public static string LibraryCacheDir(int appId) =>
        Path.Combine(SteamRoot(), "appcache", "librarycache", appId.ToString());

    private static string SteamRoot() =>
        (Registry.CurrentUser.OpenSubKey(@"Software\Valve\Steam")?.GetValue("SteamPath") as string ?? DefaultSteamPath)
        .Replace('/', '\\');

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
        foreach (Match m in PathEntry.Matches(File.ReadAllText(vdf)))
            yield return m.Groups[1].Value.Replace(@"\\", @"\");
    }
}
