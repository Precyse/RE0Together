namespace CoopLauncher;

/// <summary>The values of a game's options as the player set them: for the adapter ini, for the lobby (host options) and
/// for the seat labels.</summary>
public static class GameOptions
{
    private const string CharacterKey = "character";
    private const char PairSeparator = ';';
    private const char KeyValueSeparator = '=';

    /// <summary>Every option's current value (the player's choice if it is still offered, else the default).</summary>
    public static Dictionary<string, string> Current(GameProfile profile, string? gameDir) =>
        (profile.Options ?? []).ToDictionary(option => option.Key, option => ValueOf(profile.Id, option, gameDir));

    /// <summary>The host options' current values, which the lobby carries to the guests.</summary>
    public static Dictionary<string, string> HostValues(GameProfile profile, string? gameDir)
    {
        var host = (profile.Options ?? []).Where(option => option.Host).Select(option => option.Key).ToHashSet();
        return Current(profile, gameDir).Where(pair => host.Contains(pair.Key)).ToDictionary(pair => pair.Key, pair => pair.Value);
    }

    public static string Encode(IReadOnlyDictionary<string, string> values) =>
        string.Join(PairSeparator, values.Select(pair => $"{pair.Key}{KeyValueSeparator}{pair.Value}"));

    public static Dictionary<string, string> Decode(string? text) =>
        (text ?? string.Empty).Split(PairSeparator, StringSplitOptions.RemoveEmptyEntries)
            .Select(pair => pair.Split(KeyValueSeparator, 2))
            .Where(parts => parts.Length == 2)
            .ToDictionary(parts => parts[0], parts => parts[1]);

    /// <summary>The game's characters in seat order: the host's chosen character first.</summary>
    public static List<string> SeatCharacters(GameProfile profile, IReadOnlyDictionary<string, string> hostValues)
    {
        var characters = profile.Characters ?? [];
        var option = profile.Options?.FirstOrDefault(o => o.Key == CharacterKey);
        var chosen = option != null && hostValues.TryGetValue(CharacterKey, out var value)
            ? option.Values.FirstOrDefault(v => v.Value == value)?.Label
            : null;
        return chosen == null ? characters : characters.Where(c => c == chosen).Concat(characters.Where(c => c != chosen)).ToList();
    }

    private static string ValueOf(string gameId, GameOption option, string? gameDir)
    {
        var chosen = AppSettings.Current.Option(gameId, option.Key);
        return chosen != null && option.AvailableIn(gameDir).Any(v => v.Value == chosen) ? chosen : option.Default;
    }
}
