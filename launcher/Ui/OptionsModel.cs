namespace CoopLauncher.Gui;

/// <summary>The pure rules of a game's option rows (the session options popup and the settings view's game options), used by
/// the web window's snapshot.</summary>
internal static class OptionsModel
{
    /// <summary>The options a grid shows: the session options, or (settingsOptions) the ones marked for the settings view; the
    /// character select's own options and the ones this machine supplies at run time are never among them.</summary>
    public static List<GameOption> OptionsOf(GameProfile game, bool settingsOptions) =>
        (game.Options ?? []).Where(option => option.Settings == settingsOptions && !GameOptions.InCharacterSelect(game, option) && !GameOptions.IsRuntime(option)).ToList();

    /// <summary>Whether the game has options a grid shows.</summary>
    public static bool Has(GameProfile game, bool settingsOptions = false) => OptionsOf(game, settingsOptions).Count > 0;

    /// <summary>The value the option reads: the host's when a host value is given and offered, else the player's own.</summary>
    public static string ShownValue(GameProfile game, GameOption option, string? gameDir, IReadOnlyDictionary<string, string>? hostValues) =>
        option.Host && hostValues != null && hostValues.TryGetValue(option.Key, out var host) && option.Values.Any(v => v.Value == host)
            ? host
            : GameOptions.Current(game, gameDir)[option.Key];

    /// <summary>The values offered in this game folder (a DLC outfit only when the folder holds the DLC).</summary>
    public static List<OptionValue> Values(GameOption option, string? gameDir) => option.AvailableIn(gameDir).ToList();

    /// <summary>The option the hosting screen's save picker shows: the game's slot option while the host can still choose (before its game
    /// starts) and the save has a slot to pick; null otherwise.</summary>
    public static GameOption? SavePicker(GameProfile game, string? gameDir, SessionStep step) =>
        OptionSources.SlotOption(game) is { } option && !SessionProgress.SelectionLocked(step)
        && GameOptions.Offered(game, option, gameDir).Count() > option.Values.Count
            ? option
            : null;

    /// <summary>Saves the player's choice at once; it reaches the adapter ini when the session launches the game.</summary>
    public static void Save(string gameId, string optionKey, string value) =>
        AppSettings.Update(settings => settings.WithOption(gameId, optionKey, value));
}
