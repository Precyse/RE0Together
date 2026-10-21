using CoopLauncher;

namespace LauncherTests;

/// <summary>Points the launcher's data folder (settings, logs) at a temp folder for a test and restores everything after it.</summary>
internal sealed class TempData : IDisposable
{
    private readonly string? _previousVariable = Environment.GetEnvironmentVariable(AppData.DataRootVariable);
    private readonly AppSettings _previousSettings;

    public TempData()
    {
        Root = Path.Combine(Path.GetTempPath(), "cf_data_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(Root);
        Environment.SetEnvironmentVariable(AppData.DataRootVariable, Root);
        _previousSettings = AppSettings.Current;
    }

    public string Root { get; }

    public void Dispose()
    {
        AppSettings.Update(_ => _previousSettings);
        Environment.SetEnvironmentVariable(AppData.DataRootVariable, _previousVariable);
        Directory.Delete(Root, recursive: true);
    }
}

/// <summary>AppSettings on a temp data folder: the written file round-trips, is replaced whole and leaves the last good copy, and an
/// unreadable file falls back to it. A settings change makes GameFolders look again.</summary>
public static class AppSettingsTests
{
    public static void Run()
    {
        RoundTripWithLastGame();
        LobbyVisibilityRoundTrip();
        WriteKeepsLastGoodCopy();
        UnreadableFileFallsBack();
        GameFolderIsRememberedUntilSettingsChange();
    }

    private static string File1(TempData data) => Path.Combine(data.Root, "settings.json");

    private static void RoundTripWithLastGame()
    {
        using var data = new TempData();
        AppSettings.Update(s => s.WithLastGame("sr2").WithGameFolder("sr2", @"C:\Games\Juiced"));
        var read = AppSettings.Read();
        Check.That(read.LastGame == "sr2" && read.GameFolder("sr2") == @"C:\Games\Juiced", "the last game and a game folder survive a write and a read");
        Check.That(AppSettings.Current.LastGame == "sr2", "the current settings carry the last game");
        Check.That(!File.Exists(File1(data) + ".tmp"), "no temp file is left after a write");
    }

    private static void LobbyVisibilityRoundTrip()
    {
        using var data = new TempData();
        Check.That(new AppSettings().LobbyVisibility == LobbyVisibility.Friends, "lobbies are friends only by default");
        Check.That(AppSettings.Read().LobbyVisibility == LobbyVisibility.Friends, "no file reads the default");
        AppSettings.Update(s => s.WithLobbyVisibility(LobbyVisibility.Everyone));
        Check.That(AppSettings.Read().LobbyVisibility == LobbyVisibility.Everyone && AppSettings.Current.LobbyVisibility == LobbyVisibility.Everyone,
            "everyone survives a write and a read");
        AppSettings.Update(s => s.WithLobbyVisibility(LobbyVisibility.Friends).WithLastGame("re0"));
        var read = AppSettings.Read();
        Check.That(read.LobbyVisibility == LobbyVisibility.Friends && read.LastGame == "re0", "back to friends, beside the other settings");
        File.WriteAllText(File1(data), "{ \"CheckForUpdates\": false }");
        Check.That(AppSettings.Read().LobbyVisibility == LobbyVisibility.Friends, "a file from before the setting reads the default");
    }

    private static void WriteKeepsLastGoodCopy()
    {
        using var data = new TempData();
        AppSettings.Update(s => s.WithLastGame("re0"));
        Check.That(!File.Exists(File1(data) + ".bak"), "the first write has no earlier file to keep");
        AppSettings.Update(s => s.WithLastGame("sr2"));
        Check.That(File.Exists(File1(data) + ".bak") && File.ReadAllText(File1(data) + ".bak").Contains("re0"), "the replaced file stays as settings.json.bak");
        Check.That(File.ReadAllText(File1(data)).Contains("sr2"), "the settings file holds the newest settings");
    }

    private static void UnreadableFileFallsBack()
    {
        using var data = new TempData();
        Check.That(AppSettings.Read().LastGame == null, "no settings file: the defaults");

        AppSettings.Update(s => s.WithLastGame("re0"));
        AppSettings.Update(s => s.WithLastGame("sr2"));
        File.WriteAllText(File1(data), "{ not json");
        Check.That(AppSettings.Read().LastGame == "re0", "an unreadable settings file falls back to the last good copy");

        AppSettings.Update(s => s.WithLastGame("ds2"));
        Check.That(File.ReadAllText(File1(data) + ".bak").Contains("re0"), "an unreadable file never replaces the last good copy");
        Check.That(AppSettings.Read().LastGame == "ds2", "the next write repairs the settings file");

        File.WriteAllText(File1(data), "{ not json");
        File.WriteAllText(File1(data) + ".bak", "also not json");
        Check.That(AppSettings.Read().LastGame == null, "both files unreadable: the defaults");
    }

    private static void GameFolderIsRememberedUntilSettingsChange()
    {
        using var data = new TempData();
        var first = Path.Combine(data.Root, "first");
        var second = Path.Combine(data.Root, "second");
        Directory.CreateDirectory(first);
        Directory.CreateDirectory(second);
        var profile = new GameProfile("cf_folder_test", "Test", 1, "t.exe", 2, 1, [], "coop/adapter.ini");

        AppSettings.Update(s => s.WithGameFolder(profile.Id, first));
        Check.That(GameFolders.Find(profile) == first, "the folder chosen in the settings is found");
        Directory.Delete(first);
        Check.That(GameFolders.Find(profile) == first, "a found folder is remembered until the cache is invalidated");

        AppSettings.Update(s => s.WithGameFolder(profile.Id, second));
        Check.That(GameFolders.Find(profile) == second, "a settings change makes the next lookup start again");

        GameFolders.Invalidate();
        Directory.Delete(second);
        Check.That(GameFolders.Find(profile) != second, "after Invalidate a folder that is gone is not returned");
    }
}
