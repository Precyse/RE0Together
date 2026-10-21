using CoopLauncher;

namespace LauncherTests;

/// <summary>SavePaths.Expand: a template that cannot be expanded here is null, never an exception.</summary>
public static class SavePathsTests
{
    private const string Documents = @"C:\Users\Test\Documents";
    private const ulong SteamId = 76561197960265729;

    public static void Run()
    {
        Check.That(SavePaths.Expand("{documents}/Saves/{steamid64}", Documents, SteamId) == $@"{Documents}\Saves\{SteamId}", "both tokens expand and '/' becomes the path separator");
        Check.That(SavePaths.Expand("{documents}/Saves", Documents, null) == $@"{Documents}\Saves", "a template without {steamid64} needs no Steam user");
        Check.That(SavePaths.Expand("{documents}/Saves/{steamid64}", Documents, null) == null, "no Steam user signed in: null");
        Check.That(SavePaths.Expand("{documents}/Saves", "", SteamId) == null, "no Documents folder: null");
        Check.That(SavePaths.Expand("{appdata}/Saves", Documents, SteamId) == null, "an unknown token: null");
        Check.That(SavePaths.Expand("coop/session", Documents, null) == @"coop\session", "a plain relative template is returned as it is");
        Check.That(SavePaths.Expand("{documents}/Saves", @"C:\Users\{odd}\Documents", SteamId) == @"C:\Users\{odd}\Documents\Saves", "a brace in the Documents folder is not a token");
    }
}
