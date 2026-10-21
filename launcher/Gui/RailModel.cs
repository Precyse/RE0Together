namespace CoopLauncher.Gui;

/// <summary>How a rail row's status line is coloured: quiet gray, amber (an update), red (a problem).</summary>
internal enum RailTone { Quiet, Amber, Red }

/// <summary>The pure rules of the game rail's rows, used by the WinForms rail and by the web window's snapshot.</summary>
internal static class RailModel
{
    private const string RunningWord = "Running";
    private const string PlayersFormat = "{0} players";

    /// <summary>The line under a game's name: Running while its session runs, else its player count.</summary>
    public static string Sub(GameProfile game, string? runningGameId) =>
        game.Id == runningGameId ? RunningWord : string.Format(PlayersFormat, game.MaxPlayers);

    /// <summary>The colour of the status line: a problem is red, an update or other attention amber, else quiet.</summary>
    public static RailTone Tone(GameStatus status) => !status.NeedsAttention ? RailTone.Quiet : status.HasProblem ? RailTone.Red : RailTone.Amber;

    /// <summary>While a session is open the rail is locked and the rows other than the selected one are dimmed.</summary>
    public static bool Locked(bool railEnabled, bool selected) => !railEnabled && !selected;
}
