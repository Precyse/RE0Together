using System.Text.Json;

namespace CoopLauncher;

/// <summary>The restored window rectangle: size in 96 dpi units, position in screen pixels.</summary>
public sealed record WindowBounds(int X, int Y, int Width, int Height, bool Maximized);

/// <summary>
/// The launcher's settings, stored as JSON in %AppData%\CoopLauncher\settings.json. Read once at start; every change
/// replaces <see cref="Current"/> and writes the file. A missing or unreadable file means the defaults.
/// </summary>
public sealed record AppSettings(
    bool CheckForUpdates = true, Dictionary<string, string>? GameFolders = null, WindowBounds? Window = null,
    Dictionary<string, Dictionary<string, string>>? GameOptions = null)
{
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

    /// <summary>Replaces the settings with the changed copy and saves them.</summary>
    public static void Update(Func<AppSettings, AppSettings> change)
    {
        lock (Gate)
        {
            _current = change(_current);
            Write(_current);
        }
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

    private static AppSettings Read()
    {
        try
        {
            return File.Exists(AppData.SettingsFile)
                ? JsonSerializer.Deserialize<AppSettings>(File.ReadAllText(AppData.SettingsFile), JsonOptions) ?? new AppSettings()
                : new AppSettings();
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or JsonException)
        {
            Log.Info($"Settings unreadable, using the defaults: {e.Message}");
            return new AppSettings();
        }
    }

    private static void Write(AppSettings settings)
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(AppData.SettingsFile)!);
            File.WriteAllText(AppData.SettingsFile, JsonSerializer.Serialize(settings, JsonOptions));
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            Log.Info($"Could not save the settings: {e.Message}");
        }
    }
}
