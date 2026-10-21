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
    private const string DocumentsToken = "{documents}";
    private const string SteamIdToken = "{steamid64}";
    private const char TokenStart = '{';

    /// <summary>The template with this machine's folders in it, or null when it cannot be expanded (no Steam user signed in, no
    /// Documents folder, an unknown token).</summary>
    public static string? Expand(string template) =>
        Expand(template, Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments), SteamId64());

    /// <summary>The same rule on given values: a token without a value, or one the rule does not know, gives null.</summary>
    public static string? Expand(string template, string documents, ulong? steamId64)
    {
        if (template.Replace(DocumentsToken, "").Replace(SteamIdToken, "").Contains(TokenStart)) return null;
        if (template.Contains(DocumentsToken) && documents.Length == 0) return null;
        if (template.Contains(SteamIdToken) && steamId64 == null) return null;
        return template.Replace(DocumentsToken, documents).Replace(SteamIdToken, steamId64.ToString()).Replace('/', Path.DirectorySeparatorChar);
    }

    private static ulong? SteamId64()
    {
        var account = Registry.CurrentUser.OpenSubKey(ActiveProcessKey)?.GetValue(ActiveUserValue) as int? ?? 0;
        return account == 0 ? null : SteamId64Base + (uint)account;
    }
}
