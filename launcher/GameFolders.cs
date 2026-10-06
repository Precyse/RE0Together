namespace CoopLauncher;

/// <summary>Where a game is installed: the folder chosen in the settings, else the one Steam knows.</summary>
public static class GameFolders
{
    public static string? Find(GameProfile profile) =>
        AppSettings.Current.GameFolder(profile.Id) is { } chosen && Directory.Exists(chosen)
            ? chosen
            : SteamLibrary.FindGameDir(profile.SteamAppId);
}
