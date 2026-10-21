namespace CoopLauncher;

/// <summary>Where a game is installed: the folder chosen in the settings, else the one Steam knows. A found folder is remembered
/// per game until <see cref="Invalidate"/> (the settings changed, or the window refreshes), so a status update does not read the
/// registry and Steam's files again. A game that is not found is looked for again each time, so installing it is noticed.</summary>
public static class GameFolders
{
    private static readonly object Gate = new();
    private static readonly Dictionary<string, string> Found = new();

    public static string? Find(GameProfile profile)
    {
        lock (Gate)
        {
            if (Found.TryGetValue(profile.Id, out var remembered)) return remembered;
        }
        var folder = AppSettings.Current.GameFolder(profile.Id) is { } chosen && Directory.Exists(chosen)
            ? chosen
            : SteamLibrary.FindGameDir(profile.SteamAppId);
        if (folder != null)
        {
            lock (Gate) Found[profile.Id] = folder;
        }
        return folder;
    }

    /// <summary>Forgets every remembered folder; the next <see cref="Find"/> looks again.</summary>
    public static void Invalidate()
    {
        lock (Gate) Found.Clear();
    }
}
