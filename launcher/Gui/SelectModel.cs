namespace CoopLauncher.Gui;

/// <summary>What the character select shows for one status: the two halves of the stage, the partner's character, and the
/// outfits of the focused half's character with the one picked. Characters are the values of the profile's character option
/// and outfit keys the keys of its outfit options.</summary>
internal sealed record SelectModel(
    StageHalf You, StageHalf Partner, string PartnerCharacter, GameOption? Outfit, IReadOnlyList<OutfitTile> Tiles, string? Selected);

/// <summary>The pure mapping from the app's status to the character select (see <see cref="SelectModel"/>).
/// <para>You are always the left half. Idle, your character is your own choice, as if you were about to host; in a lobby it is
/// the seat of the local player. Everyone picks their own outfit, one per character: idle, the partner half shows your choice
/// for the partner's character; in a lobby it shows what the partner published (nothing while that is unknown).</para></summary>
internal static class SelectModels
{
    private const string YouRole = "You";
    private const string PartnerRole = "Partner";
    private const string OpenRole = "Open";

    public static SelectModel Build(GameProfile game, string? gameDir, AppStatus status, bool free, Side focus)
    {
        var characters = game.Options!.First(option => option.Key == GameOptions.CharacterKey);
        var values = GameOptions.Current(game, gameDir);
        var you = free ? values[GameOptions.CharacterKey] : LobbyCharacter(game, status) ?? values[GameOptions.CharacterKey];
        var partner = characters.Values.First(value => value.Value != you).Value;
        var partnerSlot = free ? null : status.Players.FirstOrDefault(player => !player.IsLocal);

        var yourValue = values.GetValueOrDefault(OutfitKey(game, you));
        var partnerValue = free
            ? values.GetValueOrDefault(OutfitKey(game, partner))
            : partnerSlot == null ? null : GameOptions.Decode(partnerSlot.Options).GetValueOrDefault(OutfitKey(game, partner));

        var focused = focus == Side.You ? you : partner;
        var focusedOutfit = GameOptions.OutfitOf(game, focused);
        var tiles = focusedOutfit?.AvailableIn(gameDir)
            .Select(value => new OutfitTile(value.Value, value.Label, FigureOf(game, characters, focused, value.Value))).ToList() ?? [];
        return new SelectModel(
            Half(game, characters, you, yourValue, YouRole),
            Half(game, characters, partner, partnerValue, free ? PartnerRole : partnerSlot?.Name ?? OpenRole),
            partner, focusedOutfit, tiles, focus == Side.You ? yourValue : partnerValue);
    }

    /// <summary>The character of the seat the local player is in (seats follow the players, host first), or null when the
    /// status has no such seat.</summary>
    private static string? LobbyCharacter(GameProfile game, AppStatus status)
    {
        var seat = status.Players.ToList().FindIndex(player => player.IsLocal);
        return seat >= 0 && seat < status.SeatCharacters.Count ? GameOptions.CharacterValue(game, status.SeatCharacters[seat]) : null;
    }

    private static string OutfitKey(GameProfile game, string character) => GameOptions.OutfitOf(game, character)?.Key ?? string.Empty;

    /// <summary>One half: the character's figure in the outfit, its name, the role line and the outfit's label (empty when the
    /// outfit is not known or not one of the profile's).</summary>
    private static StageHalf Half(GameProfile game, GameOption characters, string character, string? outfit, string role)
    {
        var label = GameOptions.OutfitOf(game, character)?.Values.FirstOrDefault(value => value.Value == outfit)?.Label ?? string.Empty;
        return new StageHalf(FigureOf(game, characters, character, outfit), CharacterLabel(characters, character).ToUpperInvariant(), role, label);
    }

    private static Figure FigureOf(GameProfile game, GameOption characters, string character, string? outfit) =>
        new(character, OutfitKey(game, character), outfit, CharacterLabel(characters, character)[..1]);

    private static string CharacterLabel(GameOption characters, string character) => characters.Values.First(value => value.Value == character).Label;
}
