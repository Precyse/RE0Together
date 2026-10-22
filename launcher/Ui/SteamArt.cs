namespace CoopLauncher.Gui;

/// <summary>Where a game's artwork is: the player's custom Steam art first, then Steam's local library cache. Missing art is normal
/// (never opened in the Steam library, or another Steam layout): every lookup returns null and the page shows without it.</summary>
internal static class SteamArt
{
    /// <summary>The pictures a game has in the library cache.</summary>
    public enum Kind { Capsule, Hero, Logo }

    private static readonly string[] CapsuleNames = { "library_600x900.jpg", "library_capsule.jpg" };
    private static readonly string[] HeroNames = { "library_hero.jpg" };
    private static readonly string[] LogoNames = { "logo.png" };
    private const string CustomCapsuleSuffix = "p";
    private const string CustomHeroSuffix = "_hero";
    private const string CustomLogoSuffix = "_logo";

    /// <summary>The file of a picture in the library cache (the first name of its kind that exists), or null.</summary>
    public static string? FindFile(int appId, Kind kind) => FindFiles(appId, kind).FirstOrDefault();

    /// <summary>The files of a picture's kind that exist: custom Steam art first, then the library cache in the order its names
    /// are preferred.</summary>
    private static IEnumerable<string> FindFiles(int appId, Kind kind)
    {
        var suffix = kind switch { Kind.Capsule => CustomCapsuleSuffix, Kind.Hero => CustomHeroSuffix, _ => CustomLogoSuffix };
        foreach (var custom in SteamLibrary.CustomArtFiles(appId, suffix)) yield return custom;
        var dir = SteamLibrary.LibraryCacheDir(appId);
        if (!Directory.Exists(dir)) yield break;
        var names = kind switch { Kind.Capsule => CapsuleNames, Kind.Hero => HeroNames, _ => LogoNames };
        foreach (var name in names)
            if (Directory.EnumerateFiles(dir, name, SearchOption.AllDirectories).FirstOrDefault() is { } path) yield return path;
    }
}
