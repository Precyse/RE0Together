using System.Text.RegularExpressions;
using Microsoft.Win32;

namespace CoopLauncher;

/// <summary>Finds an installed game's folder through Steam's libraryfolders.vdf and appmanifests.</summary>
public static class SteamLibrary
{
    private const string DefaultSteamPath = @"C:\Program Files (x86)\Steam";
    private static readonly Regex PathEntry = new("\"path\"\\s+\"([^\"]+)\"", RegexOptions.Compiled);
    private static readonly Regex InstallDirEntry = new("\"installdir\"\\s+\"([^\"]+)\"", RegexOptions.Compiled);

    public static string? FindGameDir(int appId)
    {
        foreach (var library in LibraryPaths())
        {
            var steamApps = Path.Combine(library, "steamapps");
            var manifest = Path.Combine(steamApps, $"appmanifest_{appId}.acf");
            if (!File.Exists(manifest)) continue;
            var match = InstallDirEntry.Match(File.ReadAllText(manifest));
            if (!match.Success) continue;
            var dir = Path.Combine(steamApps, "common", match.Groups[1].Value);
            if (Directory.Exists(dir)) return dir;
        }
        return null;
    }

    /// <summary>Steam cloud cache of a game for one account: Steam\userdata\accountId\appId\remote.</summary>
    public static string UserRemoteDir(int appId, uint accountId) =>
        Path.Combine(SteamRoot(), "userdata", accountId.ToString(), appId.ToString(), "remote");

    private static string SteamRoot() =>
        (Registry.CurrentUser.OpenSubKey(@"Software\Valve\Steam")?.GetValue("SteamPath") as string ?? DefaultSteamPath)
        .Replace('/', '\\');

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
