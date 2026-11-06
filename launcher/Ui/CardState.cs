namespace CoopLauncher.Gui;

/// <summary>What a player's card shows as its state: Idle (in the lobby or ready), InGame, Progress (connecting, checking, receiving
/// or starting), Problem (held, silent, refused, game closed, a character or save that is wrong) and Open (an empty seat).</summary>
public enum CardState { Idle, InGame, Progress, Problem, Open }

/// <summary>The pure mapping from a player's published step to the state a card shows and its words.</summary>
public static class CardStates
{
    private const string HeldWords = "Reconnecting";
    private const string SilentWords = "Not responding";
    private const string ConnectingWords = "Connecting";
    private const string ConnectedWords = "Connected";
    private const string CharacterNotLoadedWords = "Character not loaded";
    private const string SaveNotProtectedWords = "Save not protected";
    private const string DifferentSaveWords = "Different save than the host";

    /// <summary>Held, silent, a refused or closed game, a character or save that is wrong in game: Problem. Connecting, checking the
    /// build, receiving the save or starting the game: Progress. In game, or an older peer without a step that answers pings:
    /// InGame. Everything else: Idle. <paramref name="host"/> is the host's card, for the save the player should have loaded.</summary>
    public static CardState Of(PlayerSlot player, PlayerSlot? host = null)
    {
        if (player.Held || player.Silent) return CardState.Problem;
        if (player.Connecting) return CardState.Progress;
        if (player.Step == SessionStep.None && player.RttMs != null) return CardState.InGame;
        return player.Step == SessionStep.InGame && GameProblem(player, host) != null ? CardState.Problem : OfStep(player.Step);
    }

    /// <summary>The player has everything it needs for the game to start: its step is Ready and it is neither held nor silent.</summary>
    public static bool IsReady(PlayerSlot player) => player.Step == SessionStep.Ready && !player.Held && !player.Silent;

    /// <summary>The player is ready or already past it (its game starting or running): the host may start once every guest is.</summary>
    public static bool IsPrepared(PlayerSlot player) =>
        player.Step is SessionStep.Ready or SessionStep.StartingGame or SessionStep.InGame && !player.Held && !player.Silent;

    /// <summary>The state of a step alone (a roster entry of a lobby listing has no ping or hold).</summary>
    public static CardState OfStep(SessionStep step) => step switch
    {
        SessionStep.Refused or SessionStep.GameClosed or SessionStep.CharacterNotRead => CardState.Problem,
        SessionStep.CheckingBuild or SessionStep.ReceivingSave or SessionStep.ReadingCharacter or SessionStep.StartingGame => CardState.Progress,
        SessionStep.InGame => CardState.InGame,
        _ => CardState.Idle,
    };

    /// <summary>The state line of a card: held, connecting, silent, the game's own line when it has one, then the step's words (a refused
    /// player's own reason, as the refusal says it; in game what its adapter says), or "Connected" for a ping without a step; empty for
    /// a player nothing is known about.</summary>
    public static string Words(PlayerSlot player, PlayerSlot? host = null)
    {
        if (player.Held) return HeldWords;
        if (player.Connecting) return ConnectingWords;
        if (player.Silent) return SilentWords;
        if (player.Status.Length > 0) return player.Status;
        return player.Step switch
        {
            SessionStep.Lobby => "In lobby",
            SessionStep.Ready => "Ready",
            SessionStep.CheckingBuild => "Checking build",
            SessionStep.ReceivingSave => "Receiving save",
            SessionStep.ReadingCharacter => "Reading character",
            SessionStep.PickingCharacter => "Picking character",
            SessionStep.CharacterNotRead => $"Character not read: {player.Detail}",
            SessionStep.StartingGame => "Starting game",
            SessionStep.InGame => InGameWords(player, host),
            SessionStep.GameClosed => "Game closed",
            SessionStep.Refused => player.Refusal is { } kind ? RefusalWords(player, new Refusal(kind, player.Detail)) : "Refused",
            _ => player.RttMs != null ? ConnectedWords : string.Empty,
        };
    }

    private static string RefusalWords(PlayerSlot player, Refusal refusal) => player.IsLocal ? StatusText.Refusal(refusal) : StatusText.RefusalOf(refusal, player.Name);

    private static string InGameWords(PlayerSlot player, PlayerSlot? host) =>
        GameProblem(player, host) ?? player.Game?.Place switch
        {
            GamePlace.Title => "At title",
            GamePlace.Loading => "Loading",
            GamePlace.World => "In world",
            _ => "In game",
        };

    /// <summary>What is wrong in the game the adapter reports, in words, or null: a save folder that is not protected, a character that is
    /// not loaded once in the world, or a save other than the host's (a guest at its title is told which slot to load).</summary>
    private static string? GameProblem(PlayerSlot player, PlayerSlot? host)
    {
        if (player.Game is not { } game) return null;
        if (game.Save == GameSave.Refused) return SaveNotProtectedWords;
        if (game.Place == GamePlace.World && game.Character is GameCharacter.None or GameCharacter.Mismatch or GameCharacter.Refused) return CharacterNotLoadedWords;
        if (player.IsHost || host?.Game is not { Slot: >= 0 } hostGame) return null;
        if (game.Place == GamePlace.Title) return $"Load slot {hostGame.Slot} like the host";
        return game.Place == GamePlace.World && game.Slot >= 0 && game.Slot != hostGame.Slot ? DifferentSaveWords : null;
    }
}
