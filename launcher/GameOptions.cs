using System.Globalization;

namespace CoopLauncher;

/// <summary>The values of a game's options as the player set them: for the adapter ini, for the lobby (host options) and
/// for the seat labels.</summary>
public static class GameOptions
{
    public const string SeatsKey = "max_players";

    public const string CharacterKey = "character";

    /// <summary>The entry of a guest's published options that asks the host to trade characters: it holds the character the
    /// guest wants to play, and is there only until the seats show it.</summary>
    public const string SwapRequestKey = "swap";

    private const char PairSeparator = ';';
    private const char KeyValueSeparator = '=';

    /// <summary>Every option's current value (the player's choice if it is still offered, else the default; an option the save works out,
    /// like the hash of the chosen save slot, takes that).</summary>
    public static Dictionary<string, string> Current(GameProfile profile, string? gameDir)
    {
        var options = profile.Options ?? [];
        var values = options.Where(option => option.Source != OptionSources.SaveHash)
            .ToDictionary(option => option.Key, option => ValueOf(profile, option, gameDir));
        foreach (var option in options.Where(option => option.Source == OptionSources.SaveHash))
            values[option.Key] = OptionSources.SlotHash(profile, gameDir, values.GetValueOrDefault(OptionSources.SlotKey));
        return values;
    }

    /// <summary>The values an option offers on this machine: its profile values, a DLC outfit only when the game folder holds the DLC, and for
    /// an option with a source the ones this machine supplies.</summary>
    public static IEnumerable<OptionValue> Offered(GameProfile profile, GameOption option, string? gameDir) =>
        option.Source == null ? option.AvailableIn(gameDir) : option.Values.Concat(OptionSources.Values(profile, option, gameDir));

    /// <summary>An option the grids do not list because its values come from this machine at run time (the save slot picker has its own row).</summary>
    public static bool IsRuntime(GameOption option) => option.Source != null;

    /// <summary>The options that are in the character select view and not in the options grid: the character option and
    /// each character's outfit option, for a game that has a character select.</summary>
    public static bool InCharacterSelect(GameProfile profile, GameOption option) =>
        option.Character != null || (profile.HasCharacterSelect && option.Key == CharacterKey);

    /// <summary>The outfit option of a character (a value of the character option), or null.</summary>
    public static GameOption? OutfitOf(GameProfile profile, string character) =>
        profile.Options?.FirstOrDefault(option => option.Character == character);

    /// <summary>The value of the character option with this label ("Rebecca" is "rebecca"), or null.</summary>
    public static string? CharacterValue(GameProfile profile, string label) => CharacterEntry(profile, label)?.Value;

    /// <summary>The entry of the character option with this label (its full name and unit come with it), or null.</summary>
    public static OptionValue? CharacterEntry(GameProfile profile, string label) =>
        profile.Options?.FirstOrDefault(option => option.Key == CharacterKey)?.Values.FirstOrDefault(v => v.Label == label);

    /// <summary>The character (a value of the character option) of a seat in the seat order, or null when there is no such seat.</summary>
    public static string? CharacterAtSeat(GameProfile profile, IReadOnlyList<string> seatCharacters, int seat) =>
        seat >= 0 && seat < seatCharacters.Count ? CharacterValue(profile, seatCharacters[seat]) : null;

    /// <summary>The value of the character option that is not this one (a game with a character select has two), or null.</summary>
    public static string? OtherCharacter(GameProfile profile, string character) =>
        profile.Options?.FirstOrDefault(option => option.Key == CharacterKey)?.Values.FirstOrDefault(v => v.Value != character)?.Value;

    /// <summary>The host options' current values, which the lobby carries to the guests.</summary>
    public static Dictionary<string, string> HostValues(GameProfile profile, string? gameDir)
    {
        var host = (profile.Options ?? []).Where(option => option.Host).Select(option => option.Key).ToHashSet();
        return Current(profile, gameDir).Where(pair => host.Contains(pair.Key)).ToDictionary(pair => pair.Key, pair => pair.Value);
    }

    /// <summary>The current values of the options that are not the host's: what this player chose for itself, which the lobby
    /// shows to the other players.</summary>
    public static Dictionary<string, string> OwnValues(GameProfile profile, string? gameDir)
    {
        var own = (profile.Options ?? []).Where(option => !option.Host).Select(option => option.Key).ToHashSet();
        return Current(profile, gameDir).Where(pair => own.Contains(pair.Key)).ToDictionary(pair => pair.Key, pair => pair.Value);
    }

    /// <summary>The values the adapter ini gets: the player's own, except that a guest takes the host's value of every host
    /// option the lobby carries (a value the profile does not offer is ignored; an option with a source takes any value, and its default
    /// when the lobby has none), so both machines run the same character, partner mode and forced save slot.</summary>
    public static Dictionary<string, string> ForAdapter(GameProfile profile, IReadOnlyDictionary<string, string> own,
        IReadOnlyDictionary<string, string> hostValues, bool isHost)
    {
        var values = own.ToDictionary(pair => pair.Key, pair => pair.Value);
        if (isHost) return values;
        foreach (var option in (profile.Options ?? []).Where(option => option.Host))
        {
            if (hostValues.TryGetValue(option.Key, out var hostValue) && (IsRuntime(option) || option.Values.Any(v => v.Value == hostValue))) values[option.Key] = hostValue;
            else if (IsRuntime(option)) values[option.Key] = option.Default;
        }
        return values;
    }

    public static string Encode(IReadOnlyDictionary<string, string> values) =>
        string.Join(PairSeparator, values.Select(pair => $"{pair.Key}{KeyValueSeparator}{pair.Value}"));

    /// <summary>A key that appears twice keeps its last value.</summary>
    public static Dictionary<string, string> Decode(string? text)
    {
        var values = new Dictionary<string, string>();
        foreach (var parts in (text ?? string.Empty).Split(PairSeparator, StringSplitOptions.RemoveEmptyEntries).Select(pair => pair.Split(KeyValueSeparator, 2)))
            if (parts.Length == 2) values[parts[0]] = parts[1];
        return values;
    }

    /// <summary>How many players the lobby takes: the host's <see cref="SeatsKey"/> choice when the profile offers that
    /// option and the value is a number from 1 to the profile's maximum, else the maximum.</summary>
    public static int Seats(GameProfile profile, IReadOnlyDictionary<string, string> hostValues) =>
        profile.Options?.Any(option => option.Key == SeatsKey) == true
        && hostValues.TryGetValue(SeatsKey, out var text)
        && int.TryParse(text, NumberStyles.None, CultureInfo.InvariantCulture, out var seats)
        && seats >= 1 && seats <= profile.MaxPlayers
            ? seats
            : profile.MaxPlayers;

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

    private static string ValueOf(GameProfile profile, GameOption option, string? gameDir)
    {
        var chosen = AppSettings.Current.Option(profile.Id, option.Key);
        return chosen != null && Offered(profile, option, gameDir).Any(v => v.Value == chosen) ? chosen : option.Default;
    }
}
