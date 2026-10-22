namespace CoopLauncher.Gui;

/// <summary>One outfit of a character: the option value it sets, its label and the figure it shows.</summary>
internal sealed record OutfitTile(string Value, string Label, Figure Figure);

/// <summary>How a half of the stage takes part: Own is the local player's (full strength, arrows that change the outfit),
/// Partner the other seat before a session (dimmed, outlined arrows that change your choice for that character) and
/// ReadOnly somebody else's outfit in a session (no arrows, no dots).</summary>
internal enum HalfKind { Own, Partner, ReadOnly }

/// <summary>What sits between the halves: the swap button (until the game starts) or the lock (from then on).</summary>
internal enum StageCentre { Swap, Lock }

/// <summary>What one half of the stage shows: the character's figure in an outfit, its name, the role line, the outfit's label,
/// and for the halves with arrows the outfits to cycle through (<see cref="Outfits"/>, the one shown is <see cref="Index"/>).
/// Locked: the arrows are disabled.</summary>
internal sealed record StageHalf(
    Figure Figure, string Name, string Role, string Label, HalfKind Kind, bool Locked, IReadOnlyList<OutfitTile> Outfits, int Index)
{
    private const float PartnerOpacity = 0.55f;
    private const float ReadOnlyOpacity = 0.8f;

    public bool HasArrows => Kind != HalfKind.ReadOnly;

    /// <summary>The dots under the arrows: one per outfit, none for a half without arrows.</summary>
    public int DotCount => HasArrows ? Outfits.Count : 0;

    /// <summary>The arrows can be used.</summary>
    public bool CanCycle => HasArrows && !Locked && Outfits.Count > 1;

    /// <summary>How strongly the figure is drawn (0..1).</summary>
    public float Opacity => Kind switch { HalfKind.Own => 1f, HalfKind.Partner => PartnerOpacity, _ => ReadOnlyOpacity };
}

/// <summary>What the character select shows for one status: the two halves, what sits between them, and the character the swap
/// button picks (in a session the host's own choice decides, and a guest's swap asks the host for it). Characters are the values of the profile's character option and outfit keys the keys of its outfit options.</summary>
internal sealed record SelectModel(StageHalf Left, StageHalf Right, StageCentre Centre, string SwapCharacter)
{
    public const int Halves = 2;

    public StageHalf this[int half] => half == 0 ? Left : Right;

    /// <summary>The half that is the local player's.</summary>
    public int OwnHalf => Left.Kind == HalfKind.Own ? 0 : 1;
}

/// <summary>The pure mapping from the app's status to the character select (see <see cref="SelectModel"/>).
/// <para>Before a session you are the left half, as if you were about to host, with your own character choice; the right half is
/// the other character, which your swap button trades with. Everyone picks their own outfit, one per character. In a session the
/// characters follow the seats (host first): the host is the left half and a guest the right one, the other half shows what that
/// player published (nothing while it is unknown) and is read-only. The swap button stays until the game starts, when it gives
/// way to the lock and your outfit locks too.</para></summary>
internal static class SelectModels
{
    private const string YouRole = "You";
    private const string PartnerRole = "Partner";
    private const string OpenRole = "Open";
    private const string HostRole = "Host";

    public static SelectModel Build(GameProfile game, string? gameDir, AppStatus status, bool free)
    {
        var characters = game.Options!.First(option => option.Key == GameOptions.CharacterKey);
        var values = GameOptions.Current(game, gameDir);
        var mine = free ? values[GameOptions.CharacterKey] : LobbyCharacter(game, status) ?? values[GameOptions.CharacterKey];
        var other = GameOptions.OtherCharacter(game, mine)!;
        var locked = !free && SessionProgress.SelectionLocked(status.Step);
        var centre = locked ? StageCentre.Lock : StageCentre.Swap;
        var own = Choosable(game, gameDir, characters, mine, values, YouRole, HalfKind.Own, locked);
        if (free)
            return new SelectModel(own, Choosable(game, gameDir, characters, other, values, PartnerRole, HalfKind.Partner, locked: false), centre, other);

        var hosting = status.LocalHosts;
        var slot = hosting ? status.Players.FirstOrDefault(player => !player.IsLocal) : status.Players.FirstOrDefault(player => player.IsHost);
        var role = hosting ? slot?.Name ?? OpenRole : slot == null ? HostRole : $"{slot.Name} {HostRole}";
        var published = slot == null ? null : GameOptions.Decode(slot.Options).GetValueOrDefault(OutfitKey(game, other));
        var theirs = ReadOnly(game, characters, other, published, role);
        return hosting ? new SelectModel(own, theirs, centre, other) : new SelectModel(theirs, own, centre, other);
    }

    /// <summary>The outfit after (direction 1) or before (-1) the one the half shows, wrapping round.</summary>
    public static OutfitTile Neighbor(StageHalf half, int direction)
    {
        var count = half.Outfits.Count;
        return half.Outfits[((half.Index + direction) % count + count) % count];
    }

    /// <summary>The character of the seat the local player is in (seats follow the players, host first), or null when the
    /// status has no such seat.</summary>
    private static string? LobbyCharacter(GameProfile game, AppStatus status)
    {
        return GameOptions.CharacterAtSeat(game, status.SeatCharacters, status.LocalSeat);
    }

    private static string OutfitKey(GameProfile game, string character) => GameOptions.OutfitOf(game, character)?.Key ?? string.Empty;

    /// <summary>A half whose outfit is one of the player's own choices, with the outfits the arrows cycle through.</summary>
    private static StageHalf Choosable(GameProfile game, string? gameDir, GameOption characters, string character,
        IReadOnlyDictionary<string, string> values, string role, HalfKind kind, bool locked)
    {
        var outfit = values.GetValueOrDefault(OutfitKey(game, character));
        var tiles = GameOptions.OutfitOf(game, character)?.AvailableIn(gameDir)
            .Select(value => new OutfitTile(value.Value, value.Label, FigureOf(game, characters, character, value.Value))).ToList() ?? [];
        var index = Math.Max(0, tiles.FindIndex(tile => tile.Value == outfit));
        return new StageHalf(FigureOf(game, characters, character, outfit), NameOf(characters, character), role, LabelOf(game, character, outfit), kind, locked, tiles, index);
    }

    /// <summary>A half that shows what somebody else published: no arrows, and an outfit that may not be known.</summary>
    private static StageHalf ReadOnly(GameProfile game, GameOption characters, string character, string? outfit, string role) =>
        new(FigureOf(game, characters, character, outfit), NameOf(characters, character), role, LabelOf(game, character, outfit), HalfKind.ReadOnly, true, [], -1);

    private static string LabelOf(GameProfile game, string character, string? outfit) =>
        GameOptions.OutfitOf(game, character)?.Values.FirstOrDefault(value => value.Value == outfit)?.Label ?? string.Empty;

    private static string NameOf(GameOption characters, string character) => CharacterLabel(characters, character).ToUpperInvariant();

    private static Figure FigureOf(GameProfile game, GameOption characters, string character, string? outfit) =>
        new(character, OutfitKey(game, character), outfit, CharacterLabel(characters, character)[..1]);

    private static string CharacterLabel(GameOption characters, string character) => characters.Values.First(value => value.Value == character).Label;
}
