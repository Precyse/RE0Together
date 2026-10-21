using System.Text.Json;

namespace CoopLauncher;

/// <summary>The restored window rectangle: size in 96 dpi units, position in screen pixels.</summary>
public sealed record WindowBounds(int X, int Y, int Width, int Height, bool Maximized);

/// <summary>Who can find a lobby this launcher hosts: Friends = friends only (Steam's friends-only lobby), Everyone = public, listed
/// in every launcher's Lobbies tab.</summary>
public enum LobbyVisibility { Friends, Everyone }

/// <summary>
/// The launcher's settings, stored as JSON in %AppData%\CoopLauncher\settings.json. Read once at start; every change
/// replaces <see cref="Current"/> and writes the file. A file is written whole beside itself and then moved over the old one, which
/// stays as settings.json.bak: a file that turns out unreadable falls back to that copy, and a missing file means the defaults.
/// </summary>
public sealed record AppSettings(
    bool CheckForUpdates = true, Dictionary<string, string>? GameFolders = null, WindowBounds? Window = null,
    Dictionary<string, Dictionary<string, string>>? GameOptions = null, string? LastGame = null,
    LobbyVisibility LobbyVisibility = LobbyVisibility.Friends)
{
    private const string TempSuffix = ".tmp";
    private const string BackupSuffix = ".bak";

    private static readonly JsonSerializerOptions JsonOptions = new() { WriteIndented = true, PropertyNameCaseInsensitive = true };
    private static readonly object Gate = new();
    private static AppSettings _current = Read();

    public static AppSettings Current
    {
        get
        {
            lock (Gate) return _current;
        }
    }

    /// <summary>Replaces the settings with the changed copy and saves them. A changed game folder must be found again, so the
    /// remembered folders are dropped.</summary>
    public static void Update(Func<AppSettings, AppSettings> change)
    {
        lock (Gate)
        {
            _current = change(_current);
            Write(_current);
        }
        CoopLauncher.GameFolders.Invalidate();
    }

    /// <summary>The game folder the player chose for this game, or null to use the one Steam knows.</summary>
    public string? GameFolder(string gameId) => GameFolders != null && GameFolders.TryGetValue(gameId, out var folder) ? folder : null;

    /// <summary>A copy with the game's folder set; an empty folder clears it.</summary>
    public AppSettings WithGameFolder(string gameId, string? folder)
    {
        var folders = new Dictionary<string, string>(GameFolders ?? new Dictionary<string, string>());
        if (string.IsNullOrWhiteSpace(folder)) folders.Remove(gameId);
        else folders[gameId] = folder;
        return this with { GameFolders = folders };
    }

    /// <summary>A copy with the game that was selected last.</summary>
    public AppSettings WithLastGame(string gameId) => this with { LastGame = gameId };

    /// <summary>A copy with who can find the lobbies this launcher hosts.</summary>
    public AppSettings WithLobbyVisibility(LobbyVisibility visibility) => this with { LobbyVisibility = visibility };

    /// <summary>The value the player chose for a game's option, or null for the option's default.</summary>
    public string? Option(string gameId, string key) =>
        GameOptions != null && GameOptions.TryGetValue(gameId, out var options) && options.TryGetValue(key, out var value) ? value : null;

    /// <summary>A copy with the game's option set.</summary>
    public AppSettings WithOption(string gameId, string key, string value)
    {
        var all = GameOptions?.ToDictionary(game => game.Key, game => new Dictionary<string, string>(game.Value))
                  ?? new Dictionary<string, Dictionary<string, string>>();
        if (!all.TryGetValue(gameId, out var options)) all[gameId] = options = new Dictionary<string, string>();
        options[key] = value;
        return this with { GameOptions = all };
    }

    /// <summary>The settings file; when it is unreadable the last good copy; the defaults when there is no file.</summary>
    public static AppSettings Read()
    {
        if (!File.Exists(AppData.SettingsFile)) return new AppSettings();
        if (TryRead(AppData.SettingsFile, out var settings)) return settings;
        if (File.Exists(AppData.SettingsFile + BackupSuffix) && TryRead(AppData.SettingsFile + BackupSuffix, out settings))
        {
            Log.Info("Settings unreadable, using the last good copy");
            return settings;
        }
        Log.Info("Settings unreadable, using the defaults");
        return new AppSettings();
    }

    private static bool TryRead(string path, out AppSettings settings)
    {
        try
        {
            settings = JsonSerializer.Deserialize<AppSettings>(File.ReadAllText(path), JsonOptions)!;
            return settings != null;
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or JsonException)
        {
            Log.Info($"Could not read {path}: {e.Message}");
            settings = new AppSettings();
            return false;
        }
    }

    /// <summary>Writes beside the file and moves over it, so a crash never leaves half a file; the file being replaced becomes the
    /// .bak copy when it is a readable one.</summary>
    private static void Write(AppSettings settings)
    {
        try
        {
            var file = AppData.SettingsFile;
            Directory.CreateDirectory(Path.GetDirectoryName(file)!);
            File.WriteAllText(file + TempSuffix, JsonSerializer.Serialize(settings, JsonOptions));
            if (File.Exists(file) && TryRead(file, out _)) File.Copy(file, file + BackupSuffix, overwrite: true);
            File.Move(file + TempSuffix, file, overwrite: true);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            Log.Info($"Could not save the settings: {e.Message}");
        }
    }
}
