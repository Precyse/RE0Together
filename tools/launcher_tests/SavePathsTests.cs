using CoopLauncher;

namespace LauncherTests;

/// <summary>SavePaths.Expand: a template that cannot be expanded here is null, never an exception.</summary>
public static class SavePathsTests
{
    private const string Documents = @"C:\Users\Test\Documents";
    private const string LocalAppData = @"C:\Users\Test\AppData\Local";
    private const ulong SteamId = 76561197960265729;
    private const string SteamUserData = @"C:\Steam\userdata";

    private static string? Expand(string template, string documents, ulong? steamId) =>
        SavePaths.Expand(template, new SavePaths.Folders(documents, LocalAppData, steamId, SteamUserData));

    public static void Run()
    {
        Check.That(Expand("{documents}/Saves/{steamid64}", Documents, SteamId) == $@"{Documents}\Saves\{SteamId}", "both tokens expand and '/' becomes the path separator");
        Check.That(Expand("{documents}/Saves", Documents, null) == $@"{Documents}\Saves", "a template without {steamid64} needs no Steam user");
        Check.That(Expand("{documents}/Saves/{steamid64}", Documents, null) == null, "no Steam user signed in: null");
        Check.That(Expand("{documents}/Saves", "", SteamId) == null, "no Documents folder: null");
        Check.That(Expand("{appdata}/Saves", Documents, SteamId) == null, "an unknown token: null");
        Check.That(Expand("coop/session", Documents, null) == @"coop\session", "a plain relative template is returned as it is");
        Check.That(Expand("{documents}/Saves", @"C:\Users\{odd}\Documents", SteamId) == @"C:\Users\{odd}\Documents\Saves", "a brace in the Documents folder is not a token");
        Check.That(Expand("{localappdata}/Pearl Abyss/CD/save/{steamaccountid}", Documents, SteamId) == $@"{LocalAppData}\Pearl Abyss\CD\save\1", "{localappdata} and {steamaccountid} (SteamID64 minus the base) expand");
        Check.That(Expand("{localappdata}/save/{steamaccountid}", Documents, null) == null, "{steamaccountid} needs a signed-in Steam user");
        Check.That(Expand("{steamuserdata}/{steamaccountid}/2054970/remote/win64_save", Documents, SteamId) == $@"{SteamUserData}\1\2054970\remote\win64_save", "{steamuserdata} expands to Steam's userdata folder");
        Check.That(SavePaths.Expand("{steamuserdata}/x", new SavePaths.Folders(Documents, LocalAppData, SteamId, "")) == null, "no Steam folder: null");
    }
}
