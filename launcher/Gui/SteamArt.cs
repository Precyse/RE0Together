namespace CoopLauncher.Gui;

/// <summary>A game's artwork from Steam's local library cache. Missing art is normal (never opened in the Steam
/// library, or another Steam layout): every lookup returns null and the window paints without it.</summary>
internal sealed class SteamArt
{
    private static readonly string[] CapsuleNames = { "library_600x900.jpg", "library_capsule.jpg" };
    private static readonly string[] HeroNames = { "library_hero.jpg" };
    private static readonly string[] LogoNames = { "logo.png" };

    public Image? Capsule { get; }
    public Image? Hero { get; }
    public Image? Logo { get; }

    public SteamArt(int appId)
    {
        var dir = SteamLibrary.LibraryCacheDir(appId);
        if (!Directory.Exists(dir)) return;
        Capsule = Load(dir, CapsuleNames);
        Hero = Load(dir, HeroNames);
        Logo = Load(dir, LogoNames);
    }

    private static Image? Load(string dir, IEnumerable<string> names)
    {
        foreach (var name in names)
        {
            var path = Directory.EnumerateFiles(dir, name, SearchOption.AllDirectories).FirstOrDefault();
            if (path == null) continue;
            try
            {
                using var stream = File.OpenRead(path);
                using var image = Image.FromStream(stream);
                return new Bitmap(image);
            }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException or ArgumentException)
            {
                Log.Info($"Artwork {path} unreadable: {e.Message}");
            }
        }
        return null;
    }
}
