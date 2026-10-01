using Microsoft.Win32;

namespace CoopLauncher;

/// <summary>Expands save folder templates from a game profile: {documents} is the user's Documents folder (wherever
/// it is redirected, e.g. OneDrive) and {steamid64} the signed-in Steam user, read from Steam's registry key so it
/// also works with local transport.</summary>
public static class SavePaths
{
    private const ulong SteamId64Base = 76561197960265728;
    private const string ActiveProcessKey = @"Software\Valve\Steam\ActiveProcess";
    private const string ActiveUserValue = "ActiveUser";

    public static string Expand(string template) =>
        template.Replace("{documents}", Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments))
                .Replace("{steamid64}", SteamId64().ToString())
                .Replace('/', Path.DirectorySeparatorChar);

    private static ulong SteamId64()
    {
        var account = Registry.CurrentUser.OpenSubKey(ActiveProcessKey)?.GetValue(ActiveUserValue) as int? ?? 0;
        if (account == 0) throw new InvalidOperationException("No Steam user signed in (needed for {steamid64})");
        return SteamId64Base + (uint)account;
    }
}
