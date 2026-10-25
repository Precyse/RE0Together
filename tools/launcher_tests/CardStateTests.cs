using CoopLauncher;
using CoopLauncher.Gui;

namespace LauncherTests;

/// <summary>The card state a player's published step maps to, and its words.</summary>
public static class CardStateTests
{
    public static void Run()
    {
        States();
        Words();
        Ready();
    }

    private static PlayerSlot Player(SessionStep step = SessionStep.None, int? rtt = null, bool held = false, bool silent = false, string status = "") =>
        new(1, "Amy", false, false, status, step, 0, rtt, held, silent);

    private static void States()
    {
        Check.That(CardStates.Of(Player(held: true, step: SessionStep.InGame)) == CardState.Problem, "a held player is a problem even in game");
        Check.That(CardStates.Of(Player(silent: true, rtt: 40)) == CardState.Problem, "a silent player is a problem");
        Check.That(CardStates.Of(Player(SessionStep.Refused)) == CardState.Problem && CardStates.Of(Player(SessionStep.GameClosed)) == CardState.Problem, "refused and closed are problems");
        foreach (var step in new[] { SessionStep.CheckingBuild, SessionStep.ReceivingSave, SessionStep.StartingGame })
            Check.That(CardStates.Of(Player(step)) == CardState.Progress, $"{step} is progress");
        Check.That(CardStates.Of(Player(SessionStep.InGame)) == CardState.InGame, "in game is in game");
        Check.That(CardStates.Of(Player(rtt: 30)) == CardState.InGame, "an older peer without a step that answers pings is in game");
        Check.That(CardStates.Of(Player()) == CardState.Idle, "no step and no ping is idle");
        Check.That(CardStates.Of(Player(SessionStep.Lobby)) == CardState.Idle && CardStates.Of(Player(SessionStep.Ready)) == CardState.Idle, "lobby and ready are idle");
        Check.That(CardStates.Of(Player(SessionStep.Lobby, rtt: 30)) == CardState.Idle, "a ping does not make a lobby player in game");
        Check.That(CardStates.OfStep(SessionStep.ReceivingSave) == CardState.Progress && CardStates.OfStep(SessionStep.InGame) == CardState.InGame
            && CardStates.OfStep(SessionStep.None) == CardState.Idle && CardStates.OfStep(SessionStep.Refused) == CardState.Problem, "a roster step maps the same way");
    }

    private static void Ready()
    {
        Check.That(CardStates.IsReady(Player(SessionStep.Ready)), "a player on the Ready step is ready");
        foreach (var step in new[] { SessionStep.Lobby, SessionStep.CheckingBuild, SessionStep.ReceivingSave, SessionStep.StartingGame, SessionStep.InGame, SessionStep.Refused })
            Check.That(!CardStates.IsReady(Player(step)), $"{step} is not ready");
        Check.That(!CardStates.IsReady(Player(SessionStep.Ready, held: true)) && !CardStates.IsReady(Player(SessionStep.Ready, silent: true)), "a held or silent player is not ready");
    }

    private static void Words()
    {
        Check.That(CardStates.Words(Player(held: true, step: SessionStep.InGame)) == "Reconnecting", "held reads Reconnecting");
        Check.That(CardStates.Words(Player(silent: true, status: "searching 3")) == "Not responding", "silent reads Not responding");
        Check.That(CardStates.Words(Player(SessionStep.InGame, status: "searching 3")) == "searching 3", "the game's own line comes before the step");
        var expected = new (SessionStep, string)[]
        {
            (SessionStep.Lobby, "In lobby"), (SessionStep.Ready, "Ready"), (SessionStep.CheckingBuild, "Checking build"),
            (SessionStep.ReceivingSave, "Receiving save"), (SessionStep.StartingGame, "Starting game"), (SessionStep.InGame, "In game"),
            (SessionStep.GameClosed, "Game closed"), (SessionStep.Refused, "Refused"),
        };
        foreach (var (step, words) in expected) Check.That(CardStates.Words(Player(step)) == words, $"{step} reads {words}");
        Check.That(CardStates.Words(Player(rtt: 12)) == "Connected", "a ping without a step reads Connected");
        Check.That(CardStates.Words(Player()) == "", "nothing known reads nothing");
    }
}
