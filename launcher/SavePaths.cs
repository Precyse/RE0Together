using Microsoft.Win32;

namespace CoopLauncher;

/// <summary>Expands save folder templates from a game profile: {documents} is the user's Documents folder (wherever
/// it is redirected, e.g. OneDrive), {localappdata} the user's local application data folder, {steamid64} the signed-in
/// Steam user and {steamaccountid} the same user's account id (the folder name Steam and some games use), both read from
/// Steam's registry key so they also work with local transport; {steamuserdata} is Steam's own userdata folder (where
/// Steam Cloud games such as Dragon's Dogma 2 keep their saves).</summary>
public static class SavePaths
{
    private const ulong SteamId64Base = 76561197960265728;
    private const string ActiveProcessKey = @"Software\Valve\Steam\ActiveProcess";
    private const string ActiveUserValue = "ActiveUser";
    private const char TokenStart = '{';
    private const string UserDataFolder = "userdata";

    /// <summary>The folders the tokens stand for; an empty folder or a null user means "not known here".</summary>
    public sealed record Folders(string Documents, string LocalAppData, ulong? SteamId64, string SteamUserData);

    /// <summary>The template with this machine's folders in it, or null when it cannot be expanded (no Steam user signed in, no
    /// Documents folder, an unknown token).</summary>
    public static string? Expand(string template) =>
        Expand(template, new Folders(Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments),
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), SteamId64(),
            Path.Combine(SteamLibrary.SteamRoot(), UserDataFolder)));

    /// <summary>The same rule on given folders: a token without a value, or one the rule does not know, gives null.</summary>
    public static string? Expand(string template, Folders folders)
    {
        var tokens = new (string Token, string? Value)[]
        {
            ("{documents}", folders.Documents.Length > 0 ? folders.Documents : null),
            ("{localappdata}", folders.LocalAppData.Length > 0 ? folders.LocalAppData : null),
            ("{steamid64}", folders.SteamId64?.ToString()),
            ("{steamaccountid}", folders.SteamId64 is { } id ? (id - SteamId64Base).ToString() : null),
            ("{steamuserdata}", folders.SteamUserData.Length > 0 ? folders.SteamUserData : null),
        };
        if (tokens.Aggregate(template, (rest, t) => rest.Replace(t.Token, "")).Contains(TokenStart)) return null;
        var expanded = template;
        foreach (var (token, value) in tokens)
        {
            if (!template.Contains(token)) continue;
            if (value == null) return null;
            expanded = expanded.Replace(token, value);
        }
        return expanded.Replace('/', Path.DirectorySeparatorChar);
    }

    /// <summary>The signed-in Steam user's account id (the folder name under Steam's userdata), or null when nobody is signed in.</summary>
    public static uint? SteamAccountId()
    {
        var account = Registry.CurrentUser.OpenSubKey(ActiveProcessKey)?.GetValue(ActiveUserValue) as int? ?? 0;
        return account == 0 ? null : (uint)account;
    }

    /// <summary>The SteamID64 of a Steam account id.</summary>
    public static ulong SteamId64Of(uint accountId) => SteamId64Base + accountId;

    private static ulong? SteamId64() => SteamAccountId() is { } account ? SteamId64Of(account) : null;
}
