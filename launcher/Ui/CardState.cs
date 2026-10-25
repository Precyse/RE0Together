namespace CoopLauncher.Gui;

/// <summary>What a player's card shows as its state: Idle (in the lobby or ready), InGame, Progress (checking, receiving or
/// starting), Problem (held, silent, refused, game closed) and Open (an empty seat).</summary>
public enum CardState { Idle, InGame, Progress, Problem, Open }

/// <summary>The pure mapping from a player's published step to the state a card shows and its words.</summary>
public static class CardStates
{
    private const string HeldWords = "Reconnecting";
    private const string SilentWords = "Not responding";
    private const string ConnectedWords = "Connected";

    /// <summary>Held, silent, a refused or closed game: Problem. Checking the build, receiving the save or starting the game:
    /// Progress. In game, or an older peer without a step that answers pings: InGame. Everything else: Idle.</summary>
    public static CardState Of(PlayerSlot player)
    {
        if (player.Held || player.Silent) return CardState.Problem;
        if (player.Step == SessionStep.None && player.RttMs != null) return CardState.InGame;
        return OfStep(player.Step);
    }

    /// <summary>The player has everything it needs for the game to start: its step is Ready and it is neither held nor silent.</summary>
    public static bool IsReady(PlayerSlot player) => player.Step == SessionStep.Ready && !player.Held && !player.Silent;

    /// <summary>The state of a step alone (a roster entry of a lobby listing has no ping or hold).</summary>
    public static CardState OfStep(SessionStep step) => step switch
    {
        SessionStep.Refused or SessionStep.GameClosed => CardState.Problem,
        SessionStep.CheckingBuild or SessionStep.ReceivingSave or SessionStep.StartingGame => CardState.Progress,
        SessionStep.InGame => CardState.InGame,
        _ => CardState.Idle,
    };

    /// <summary>The state line of a card: held, silent, the game's own line when it has one, then the step's words, or
    /// "Connected" for a ping without a step; empty for a player nothing is known about.</summary>
    public static string Words(PlayerSlot player)
    {
        if (player.Held) return HeldWords;
        if (player.Silent) return SilentWords;
        if (player.Status.Length > 0) return player.Status;
        return player.Step switch
        {
            SessionStep.Lobby => "In lobby",
            SessionStep.Ready => "Ready",
            SessionStep.CheckingBuild => "Checking build",
            SessionStep.ReceivingSave => "Receiving save",
            SessionStep.StartingGame => "Starting game",
            SessionStep.InGame => "In game",
            SessionStep.GameClosed => "Game closed",
            SessionStep.Refused => "Refused",
            _ => player.RttMs != null ? ConnectedWords : string.Empty,
        };
    }
}
