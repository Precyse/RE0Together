namespace CoopLauncher.Gui;

/// <summary>How the players of a session are laid out: wide (two seats, one under the other), tall (two or four seats, two
/// columns), mini and mini portrait (the always-on-top window).</summary>
internal enum CardLayout { Wide, Tall, Mini, MiniPortrait }

/// <summary>A character in an outfit as a portrait or tile shows it. A null value is an outfit that is not known and shows the
/// character's silhouette; the saved outfit shows its default picture (<see cref="GameOption.PictureOf"/>). The initial stands in
/// when the character has no art at all.</summary>
internal sealed record Figure(string Character, string OptionKey, string? Value, string Initial)
{
    /// <summary>A character in an outfit choice of the game's outfit option (null: not known, "": the saved outfit).</summary>
    public static Figure Of(GameProfile game, string character, string? outfit, string initial)
    {
        var option = GameOptions.OutfitOf(game, character);
        return new Figure(character, option?.Key ?? string.Empty, option?.PictureOf(outfit) ?? outfit, initial);
    }
}

/// <summary>One seat as a card shows it. Corner is the text at the right of the name (You, the ping, the hold countdown), Detail
/// the line under it (character and outfit), Words the state line, Footer the text at the right of the state row (Host).
/// Steps is the mini step list of a joining player, Progress the save transfer (0..1) while this seat receives it, Countdown
/// the hold countdown of the compact layouts. An open seat is a card in the Open state. Figure is the outfit cutout, shown
/// instead of the Steam avatar. The field file of a game with characters: Unit the character's unit, Agent its full name, Outfit
/// the outfit's label, Ready that the player is ready to start (<see cref="CardStates.IsReady"/>).</summary>
internal sealed record PlayerCard(ulong AvatarId, string Name, string Corner, string Detail, string Words, string Footer,
    CardState State, bool Local, Figure? Figure, IReadOnlyList<StepRow> Steps, double? Progress, string Countdown,
    string Unit = "", string Agent = "", string Outfit = "", bool Ready = false)
{
    private const int PercentOfFull = 100;

    /// <summary>The text at the right of a compact card or a state row: the save percent while one arrives, else the hold countdown.</summary>
    public string RightText => Progress is { } share ? $"{(int)Math.Round(share * PercentOfFull)}%" : Countdown;
}
