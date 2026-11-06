using System.Globalization;
using System.Text;

namespace CoopLauncher;

/// <summary>Where the game is: at its title screen, loading, or in the world.</summary>
public enum GamePlace { Unknown, Title, Loading, World }

/// <summary>Whether the game holds the character it should: loaded (ok), none read, another than the host's (mismatch), refused, or not
/// applicable because this player is not a guest that brings a character (off).</summary>
public enum GameCharacter { Unknown, Ok, None, Mismatch, Refused, Off }

/// <summary>Whether the game's save folder is redirected to the session's (ok), the redirect was refused, or none is wanted (off).</summary>
public enum GameSave { Unknown, Ok, Refused, Off }

/// <summary>What the adapter says about the game it runs in, from its GAME_STATE message (docs/CONTRACT.md 0x0063): the text
/// <c>place=world;char=ok;save=ok;slot=2</c>, sent when the adapter links and whenever a part changes. A key left out stays unknown;
/// <see cref="Slot"/> is the save slot the game plays (-1: not known). The same pairs travel in the player's published step.</summary>
public sealed record GameState(
    GamePlace Place = GamePlace.Unknown, GameCharacter Character = GameCharacter.Unknown, GameSave Save = GameSave.Unknown, int Slot = GameState.UnknownSlot)
{
    public const int UnknownSlot = -1;

    internal const string PlaceKey = "place";
    internal const string CharKey = "char";
    internal const string SaveKey = "save";
    internal const string SlotKey = "slot";

    private const char PairSeparator = ';';
    private const char KeyValueSeparator = '=';

    public bool IsEmpty => this == new GameState();

    /// <summary>The state in a GAME_STATE payload; a pair that is malformed or unknown is skipped.</summary>
    public static GameState Parse(byte[] payload) => FromPairs(Pairs(Encoding.ASCII.GetString(payload)));

    internal static Dictionary<string, string> Pairs(string text) =>
        text.Split(PairSeparator, StringSplitOptions.RemoveEmptyEntries)
            .Select(pair => pair.Split(KeyValueSeparator, 2)).Where(parts => parts.Length == 2)
            .GroupBy(parts => parts[0]).ToDictionary(group => group.Key, group => group.Last()[1]);

    internal static GameState FromPairs(IReadOnlyDictionary<string, string> pairs) => new(
        Name(pairs, PlaceKey, GamePlace.Unknown), Name(pairs, CharKey, GameCharacter.Unknown), Name(pairs, SaveKey, GameSave.Unknown),
        pairs.TryGetValue(SlotKey, out var slot) && int.TryParse(slot, NumberStyles.AllowLeadingSign, CultureInfo.InvariantCulture, out var number) ? number : UnknownSlot);

    /// <summary>The known parts as <c>key=value</c> pairs, in a fixed order.</summary>
    internal IEnumerable<string> EncodePairs()
    {
        if (Place != GamePlace.Unknown) yield return $"{PlaceKey}{KeyValueSeparator}{Place.ToString().ToLowerInvariant()}";
        if (Character != GameCharacter.Unknown) yield return $"{CharKey}{KeyValueSeparator}{Character.ToString().ToLowerInvariant()}";
        if (Save != GameSave.Unknown) yield return $"{SaveKey}{KeyValueSeparator}{Save.ToString().ToLowerInvariant()}";
        if (Slot != UnknownSlot) yield return string.Create(CultureInfo.InvariantCulture, $"{SlotKey}{KeyValueSeparator}{Slot}");
    }

    private static T Name<T>(IReadOnlyDictionary<string, string> pairs, string key, T unknown) where T : struct, Enum =>
        pairs.TryGetValue(key, out var text) && Enum.TryParse<T>(text, ignoreCase: true, out var value) && Enum.IsDefined(value) && !char.IsDigit(text[0]) ? value : unknown;
}
