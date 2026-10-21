using CoopLauncher;
using CoopLauncher.Gui;

namespace LauncherTests;

/// <summary>The card state a player's published step maps to, its words and tint, and the state edge's pure parts (glow levels,
/// the border path the comet follows).</summary>
public static class CardStateTests
{
    private const double Tolerance = 1e-6;

    public static void Run()
    {
        States();
        Words();
        Tints();
        GlowLevels();
        Perimeter();
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

    private static void Tints()
    {
        var green = CardStates.Tint(CardState.InGame);
        Check.That(green.A == 51 && green.G > green.R, "in game is a 20 % green");
        Check.That(CardStates.Tint(CardState.Progress).A == 46 && CardStates.Tint(CardState.Problem).A == 56, "progress is 18 % and a problem 22 %");
        Check.That(CardStates.Tint(CardState.Idle).A == 0 && CardStates.Tint(CardState.Open).A == 0, "idle and open have no tint");
    }

    private static void GlowLevels()
    {
        Check.That(StateEdge.GlowLevel(CardState.Idle, 0.5, 0.9, false) == 0 && StateEdge.GlowLevel(CardState.Open, 0.5, 0.9, false) == 0, "idle and open do not glow");
        Check.That(Math.Abs(StateEdge.GlowLevel(CardState.Progress, 0, 0, false) - 0.15) < Tolerance
            && Math.Abs(StateEdge.GlowLevel(CardState.Progress, 0.5, 0, false) - 0.35) < Tolerance, "progress breathes between 15 and 35 %");
        Check.That(Math.Abs(StateEdge.GlowLevel(CardState.Problem, 0, 0, false) - 0.25) < Tolerance
            && Math.Abs(StateEdge.GlowLevel(CardState.Problem, 0.5, 0, false) - 0.6) < Tolerance, "a problem pulses between 25 and 60 %");
        Check.That(Math.Abs(StateEdge.GlowLevel(CardState.Progress, 1, 0, false) - 0.15) < Tolerance, "the breathing joins at the end of its lap");
        Check.That(StateEdge.GlowLevel(CardState.InGame, 0.5, 0.8, false) == 0.8, "in game follows the bloom");
        Check.That(StateEdge.GlowLevel(CardState.InGame, 0.5, 0.8, true) == 0.35 && StateEdge.GlowLevel(CardState.Progress, 0.5, 0, true) == 0.25
            && StateEdge.GlowLevel(CardState.Problem, 0.5, 0, true) == 0.4, "reduced motion shows the rest values 35, 25 and 40 %");
        Check.That(StateEdge.Breathe(0.25) > 0 && StateEdge.Breathe(0.25) < 0.5, "the breathing eases in and out");
    }

    private static void Perimeter()
    {
        var box = new RectangleF(0, 0, 100, 50);
        Check.That(StateEdge.PerimeterPoints(box, 90, 130).SequenceEqual(new[] { new PointF(90, 0), new PointF(100, 0), new PointF(100, 30) }),
            "a stretch round the first corner is its three points");
        var down = StateEdge.PerimeterPoints(box, 120, 140);
        Check.That(down.Count == 2 && down[0] == new PointF(100, 20) && down[1] == new PointF(100, 40), "a stretch on one side has no corner");
        Check.That(StateEdge.PerimeterPoints(box, 290, 310).SequenceEqual(new[] { new PointF(0, 10), new PointF(0, 0), new PointF(10, 0) }),
            "a stretch past a lap turns the top-left corner");
        Check.That(StateEdge.PerimeterPoints(box, -10, 10).SequenceEqual(new[] { new PointF(0, 10), new PointF(0, 0), new PointF(10, 0) }),
            "a negative start counts back from the corner");
        Check.That(StateEdge.PerimeterPoints(box, 230, 270).SequenceEqual(new[] { new PointF(20, 50), new PointF(0, 50), new PointF(0, 30) }),
            "the bottom runs right to left, then up the left side");
    }
}
