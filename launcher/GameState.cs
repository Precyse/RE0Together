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

/// <summary>Whether the game shows the other player's copy: ok, refused (<see cref="GameState.CopyWhy"/> says why: the adapter's short
/// code), or not wanted (off).</summary>
public enum GameCopy { Unknown, Ok, Refused, Off }

/// <summary>Whether the game loaded the save it was told to: ok, failed, or there was none to load.</summary>
public enum GameLoad { Unknown, Ok, Failed, None }

/// <summary>What the adapter says about the game it runs in, from its GAME_STATE message (docs/CONTRACT.md 0x0063): the text
/// <c>place=world;char=ok;save=ok;slot=2;copy=refused:silent;peers=1234:2</c>, sent when the adapter links and whenever a part changes.
/// A key left out stays unknown; <see cref="Slot"/> is the save slot the game plays (-1: not known); <see cref="ModDiffers"/> is
/// set when a peer's adapter is not this adapter (<c>peers</c> state 2); <see cref="Coop"/> is <c>coop=0|1</c> (null: not said: 0 is a game that
/// booted without co-op) and <see cref="Load"/> is <c>load=ok|failed|none</c>. The same pairs travel in the player's published step.</summary>
public sealed record GameState(
    GamePlace Place = GamePlace.Unknown, GameCharacter Character = GameCharacter.Unknown, GameSave Save = GameSave.Unknown, int Slot = GameState.UnknownSlot,
    GameCopy Copy = GameCopy.Unknown, string CopyWhy = "", bool ModDiffers = false, bool? Coop = null, GameLoad Load = GameLoad.Unknown)
{
    public const int UnknownSlot = -1;

    internal const string PlaceKey = "place";
    internal const string CharKey = "char";
    internal const string SaveKey = "save";
    internal const string SlotKey = "slot";
    internal const string CopyKey = "copy";
    internal const string DiffersKey = "differs";
    internal const string CoopKey = "coop";
    internal const string LoadKey = "load";

    private const string PeersKey = "peers";
    private const char PairSeparator = ';';
    private const char KeyValueSeparator = '=';
    private const char ReasonSeparator = ':';
    private const char PeerListSeparator = ',';
    private const string DifferingPeerState = "2";
    private const string Set = "1";
    private const string Unset = "0";

    public bool IsEmpty => this == new GameState();

    /// <summary>The state in a GAME_STATE payload; a pair that is malformed or unknown is skipped.</summary>
    public static GameState Parse(byte[] payload) => FromPairs(Pairs(Encoding.ASCII.GetString(payload)));

    internal static Dictionary<string, string> Pairs(string text) =>
        text.Split(PairSeparator, StringSplitOptions.RemoveEmptyEntries)
            .Select(pair => pair.Split(KeyValueSeparator, 2)).Where(parts => parts.Length == 2)
            .GroupBy(parts => parts[0]).ToDictionary(group => group.Key, group => group.Last()[1]);

    internal static GameState FromPairs(IReadOnlyDictionary<string, string> pairs) => new(
        Name(pairs, PlaceKey, GamePlace.Unknown), Name(pairs, CharKey, GameCharacter.Unknown), Name(pairs, SaveKey, GameSave.Unknown),
        pairs.TryGetValue(SlotKey, out var slot) && int.TryParse(slot, NumberStyles.AllowLeadingSign, CultureInfo.InvariantCulture, out var number) ? number : UnknownSlot,
        CopyOf(pairs, out var why), why, pairs.GetValueOrDefault(DiffersKey) == Set || AnyPeerDiffers(pairs.GetValueOrDefault(PeersKey, string.Empty)),
        pairs.GetValueOrDefault(CoopKey) switch { Set => true, Unset => false, _ => null }, Name(pairs, LoadKey, GameLoad.Unknown));

    /// <summary>The known parts as <c>key=value</c> pairs, in a fixed order.</summary>
    internal IEnumerable<string> EncodePairs()
    {
        if (Place != GamePlace.Unknown) yield return $"{PlaceKey}{KeyValueSeparator}{Place.ToString().ToLowerInvariant()}";
        if (Character != GameCharacter.Unknown) yield return $"{CharKey}{KeyValueSeparator}{Character.ToString().ToLowerInvariant()}";
        if (Save != GameSave.Unknown) yield return $"{SaveKey}{KeyValueSeparator}{Save.ToString().ToLowerInvariant()}";
        if (Slot != UnknownSlot) yield return string.Create(CultureInfo.InvariantCulture, $"{SlotKey}{KeyValueSeparator}{Slot}");
        if (Copy != GameCopy.Unknown) yield return $"{CopyKey}{KeyValueSeparator}{Copy.ToString().ToLowerInvariant()}{(CopyWhy.Length > 0 ? ReasonSeparator + CopyWhy : string.Empty)}";
        if (ModDiffers) yield return $"{DiffersKey}{KeyValueSeparator}{Set}";
        if (Coop is { } coop) yield return $"{CoopKey}{KeyValueSeparator}{(coop ? Set : Unset)}";
        if (Load != GameLoad.Unknown) yield return $"{LoadKey}{KeyValueSeparator}{Load.ToString().ToLowerInvariant()}";
    }

    /// <summary>The copy part <c>refused:silent</c>: the state, and after the colon the adapter's short code.</summary>
    private static GameCopy CopyOf(IReadOnlyDictionary<string, string> pairs, out string why)
    {
        why = string.Empty;
        if (!pairs.TryGetValue(CopyKey, out var text)) return GameCopy.Unknown;
        var parts = text.Split(ReasonSeparator, 2);
        if (parts.Length == 2) why = parts[1];
        return Name(parts[0], GameCopy.Unknown);
    }

    /// <summary>Whether any <c>&lt;steamid&gt;:&lt;state&gt;</c> of a peers list has the state of an adapter that is not ours.</summary>
    private static bool AnyPeerDiffers(string peers) =>
        peers.Split(PeerListSeparator, StringSplitOptions.RemoveEmptyEntries).Any(peer => peer.Split(ReasonSeparator).LastOrDefault() == DifferingPeerState);

    private static T Name<T>(IReadOnlyDictionary<string, string> pairs, string key, T unknown) where T : struct, Enum =>
        pairs.TryGetValue(key, out var text) ? Name(text, unknown) : unknown;

    private static T Name<T>(string text, T unknown) where T : struct, Enum =>
        text.Length > 0 && Enum.TryParse<T>(text, ignoreCase: true, out var value) && Enum.IsDefined(value) && !char.IsDigit(text[0]) ? value : unknown;
}
