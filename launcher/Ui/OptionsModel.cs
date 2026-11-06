namespace CoopLauncher.Gui;

/// <summary>The pure rules of a game's option rows (the session options popup and the settings view's game options), used by
/// the web window's snapshot.</summary>
internal static class OptionsModel
{
    /// <summary>The options a grid shows: the session options, or (settingsOptions) the ones marked for the settings view; the
    /// character select's own options and the ones this machine supplies at run time are never among them, except the campaign
    /// and character picks (<see cref="OptionSources.IsChoice"/>).</summary>
    public static List<GameOption> OptionsOf(GameProfile game, bool settingsOptions) =>
        (game.Options ?? []).Where(option => option.Settings == settingsOptions && !GameOptions.InCharacterSelect(game, option) &&
                                             (!GameOptions.IsRuntime(option) || OptionSources.IsChoice(option))).ToList();

    /// <summary>Whether the game has options a grid shows.</summary>
    public static bool Has(GameProfile game, bool settingsOptions = false) => OptionsOf(game, settingsOptions).Count > 0;

    /// <summary>The value the option reads: the host's when a host value is given and offered, else the player's own.</summary>
    public static string ShownValue(GameProfile game, GameOption option, string? gameDir, IReadOnlyDictionary<string, string>? hostValues) =>
        option.Host && hostValues != null && hostValues.TryGetValue(option.Key, out var host) && option.Values.Any(v => v.Value == host)
            ? host
            : GameOptions.Current(game, gameDir)[option.Key];

    /// <summary>The values offered in this game folder (a DLC outfit only when the folder holds the DLC; the campaigns and characters
    /// this machine has).</summary>
    public static List<OptionValue> Values(GameProfile game, GameOption option, string? gameDir) => GameOptions.Offered(game, option, gameDir).ToList();

    /// <summary>The options the hosting screen offers while this machine's game has not started: the host's campaign and the game's slot
    /// option (when the save has a slot to pick); a guest's character.</summary>
    public static List<GameOption> SessionPickers(GameProfile game, string? gameDir, SessionStep step, bool isHost)
    {
        if (SessionProgress.SelectionLocked(step)) return [];
        var source = isHost ? OptionSources.Campaigns : OptionSources.Characters;
        var pickers = (game.Options ?? []).Where(option => option.Source == source).ToList();
        if (isHost && OptionSources.SlotOption(game) is { } slot && GameOptions.Offered(game, slot, gameDir).Count() > slot.Values.Count) pickers.Add(slot);
        return pickers;
    }

    /// <summary>Saves the player's choice at once; it reaches the adapter ini when the session launches the game.</summary>
    public static void Save(string gameId, string optionKey, string value) =>
        AppSettings.Update(settings => settings.WithOption(gameId, optionKey, value));
}
