namespace CoopLauncher.Gui;

/// <summary>The pure rules of a game's section in the settings view, used by the web window's snapshot.
/// <c>free</c> is whether no session is open.</summary>
internal static class SettingsModel
{
    /// <summary>The line under the game's folder: the mod and the game's build.</summary>
    public static string Line(GameStatus status) => status.SettingsLine;

    public static bool BrowseEnabled(bool free) => free;

    /// <summary>Reset returns to the folder Steam knows; it is offered once the player chose another.</summary>
    public static bool ResetEnabled(GameProfile game, bool free) => free && AppSettings.Current.GameFolder(game.Id) != null;

    /// <summary>Uninstall needs no session, no staged update and an installed mod in a found game.</summary>
    public static bool UninstallEnabled(GameStatus status, bool free, bool modsLocked) => free && !modsLocked && status.GameFound && status.ModInstalled;

    /// <summary>The folder of the game's saved sessions, or null for a game without save sync or without a folder.</summary>
    public static string? ArchiveFolder(GameProfile game)
    {
        if (game.SaveSync is not { } sync || GameFolders.Find(game) is not { } gameDir) return null;
        return AdapterSettings.ArchiveRoot(Path.Combine(gameDir, sync.SessionDir));
    }

    /// <summary>The Archive button shows once a session was saved.</summary>
    public static bool ArchiveShown(GameProfile game) => ArchiveFolder(game) is { } folder && Directory.Exists(folder);
}
