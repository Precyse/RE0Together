using System.Globalization;

namespace CoopLauncher.Gui;

/// <summary>The pure mappings from the app's status to the session screens (joining, hosting and lobby, the mini window); unit
/// tested (<c>SessionScreensTests</c>). This part is the joining screen and the helpers all three share.</summary>
internal static partial class SessionScreens
{
    /// <summary>How long Starting game lasts before Start again is offered.</summary>
    public const long StartAgainAfterMs = 10_000;

    private const double PercentScale = 100.0;
    private const double BytesPerMb = 1048576.0;
    private const string FileGap = "  ";
    private const string UnknownHost = "host";
    private const string SeatLineFormat = "Player {0} of {1}";

    private enum JoinStep { Lobby, BuildCheck, ReceivingSave, Waiting, Starting, InGame }

    /// <summary>The joining screen: the host and the seat, the steps of this game's profile up to the one the status is at (a
    /// refusal fails the step it belongs to), the buttons' state. <paramref name="target"/> is what the player asked to join, null
    /// for a typed code; the host is then read from the lobby once it is known.</summary>
    public static JoiningModel Joining(AppStatus s, GameProfile g, GameStatus mod, JoinTarget? target, GameCondition c, int newerBuild, long nowMs)
    {
        var host = s.Players.FirstOrDefault(player => player.IsHost);
        var hostName = FirstText(target?.HostName, host?.Name, s.Listing?.HostName);
        var hostId = target?.HostId ?? host?.Id ?? s.Listing?.HostId ?? 0;
        var fix = s.Refusal == null ? NoticeFix.None : SessionCards.FixFor(s, newerBuild);
        var label = fix == NoticeFix.None ? string.Empty : SessionCards.NoticeFor(s, newerBuild)?.Action?.ToUpperInvariant() ?? string.Empty;
        var starting = s.Step == SessionStep.StartingGame && s.StepSinceMs is { } since && nowMs - since >= StartAgainAfterMs;
        return new JoiningModel(
            hostName.Length > 0 ? $"Joining {hostName}" : "Joining", SeatLine(s, g), hostId, hostName,
            JoiningSteps(s, g, hostName, nowMs), fix, label, FixEnabled(fix, mod),
            StartAgain: starting || c is GameCondition.NotStarted or GameCondition.Exited);
    }

    /// <summary>Whole seconds as <c>m:ss</c> (minutes keep counting past an hour).</summary>
    public static string MinutesSeconds(long seconds)
    {
        var span = TimeSpan.FromSeconds(Math.Max(0, seconds));
        return string.Create(CultureInfo.InvariantCulture, $"{(int)span.TotalMinutes}:{span.Seconds:00}");
    }

    /// <summary>Whole seconds as <c>h:mm:ss</c>.</summary>
    public static string HoursMinutesSeconds(long seconds)
    {
        var span = TimeSpan.FromSeconds(Math.Max(0, seconds));
        return string.Create(CultureInfo.InvariantCulture, $"{(int)span.TotalHours}:{span.Minutes:00}:{span.Seconds:00}");
    }

    /// <summary>The members in seat order: the host first, then the others as the status lists them.</summary>
    private static List<PlayerSlot> Ordered(AppStatus s) => s.Players.OrderByDescending(player => player.IsHost).ToList();

    private static string FirstText(params string?[] texts) => texts.FirstOrDefault(text => !string.IsNullOrEmpty(text)) ?? string.Empty;

    private static string SeatLine(AppStatus s, GameProfile g)
    {
        var seat = Ordered(s).FindIndex(player => player.IsLocal);
        if (seat < 0) return string.Empty;
        if (seat < s.SeatCharacters.Count) return s.SeatCharacters[seat];
        return (g.Characters?.Count ?? 0) == 0 && s.Seats > 0 ? string.Format(SeatLineFormat, seat + 1, s.Seats) : string.Empty;
    }

    /// <summary>Installing or updating the mod needs the game closed; every other fix is always usable.</summary>
    private static bool FixEnabled(NoticeFix fix, GameStatus mod) => !(fix is NoticeFix.InstallMod or NoticeFix.UpdateMod && mod.GameRunning);

    private static IReadOnlyList<StepRow> JoiningSteps(AppStatus s, GameProfile g, string hostName, long nowMs)
    {
        var bridged = g.Bridged(s.HostOptions);
        var steps = new List<JoinStep> { JoinStep.Lobby };
        if (bridged) steps.Add(JoinStep.BuildCheck);
        if (bridged && g.SaveSync != null) steps.Add(JoinStep.ReceivingSave);
        steps.AddRange([JoinStep.Waiting, JoinStep.Starting, JoinStep.InGame]);

        var failed = s.Refusal is { } refusal ? RefusalStep(refusal.Kind) : (JoinStep?)null;
        var current = failed ?? StepOf(s.Step);
        var at = failed == null && current == JoinStep.InGame ? steps.Count : steps.FindIndex(step => step >= current);

        var rows = new List<StepRow>();
        for (var i = 0; i < steps.Count; i++)
        {
            var mark = i < at ? StepMark.Done : i > at ? StepMark.Pending : failed != null ? StepMark.Failed : StepMark.Active;
            rows.Add(JoinRow(steps[i], mark, s, hostName, nowMs));
        }
        return rows;
    }

    private static StepRow JoinRow(JoinStep step, StepMark mark, AppStatus s, string hostName, long nowMs)
    {
        if (mark == StepMark.Failed) return FailedRow(s.Refusal!);
        var active = mark == StepMark.Active;
        return step switch
        {
            JoinStep.Lobby => new StepRow("Lobby", mark),
            JoinStep.BuildCheck => new StepRow("Build check", mark),
            JoinStep.ReceivingSave => active ? ReceivingRow(s) : new StepRow("Receiving save", mark),
            JoinStep.Waiting => new StepRow($"Waiting for {(hostName.Length > 0 ? hostName : UnknownHost)}", mark),
            JoinStep.Starting => active
                ? new StepRow("Starting game", mark, SinceTimer(s.StepSinceMs, nowMs), Indeterminate: true)
                : new StepRow("Starting game", mark),
            _ => new StepRow("In game", mark),
        };
    }

    private static StepRow ReceivingRow(AppStatus s)
    {
        var line = s.SaveFile.Length > 0 && s.SaveTotal > 0
            ? new[] { $"{s.SaveFile}{FileGap}{Megabytes(s.SaveBytes)} / {Megabytes(s.SaveTotal)} MB" }
            : null;
        return new StepRow("Receiving save", StepMark.Active, $"{s.SavePercent}%", s.SavePercent / PercentScale, Lines: line);
    }

    private static StepRow FailedRow(Refusal refusal) => new(
        StatusText.Refusal(refusal), StepMark.Failed,
        Pairs: RefusalVersions.TryParse(refusal.Detail, out var host, out var you)
            ? new[] { ("Host", host.ToString(CultureInfo.InvariantCulture)), ("You", you.ToString(CultureInfo.InvariantCulture)) }
            : null);

    private static string Megabytes(long bytes) => (bytes / BytesPerMb).ToString("0.0", CultureInfo.InvariantCulture);

    private static string SinceTimer(long? sinceMs, long nowMs) => sinceMs is { } since ? MinutesSeconds((nowMs - since) / 1000) : string.Empty;

    /// <summary>The step a refusal belongs to.</summary>
    private static JoinStep RefusalStep(RefusalKind kind) => kind switch
    {
        RefusalKind.LauncherBuild or RefusalKind.GameBuild or RefusalKind.ModFiles or RefusalKind.ModNotInstalled => JoinStep.BuildCheck,
        RefusalKind.SaveTimedOut or RefusalKind.SaveFolderMissing => JoinStep.ReceivingSave,
        RefusalKind.LaunchFailed or RefusalKind.GameUnsupported or RefusalKind.PortInUse => JoinStep.Starting,
        _ => JoinStep.Lobby,
    };

    private static JoinStep StepOf(SessionStep step) => step switch
    {
        SessionStep.CheckingBuild => JoinStep.BuildCheck,
        SessionStep.ReceivingSave => JoinStep.ReceivingSave,
        SessionStep.Ready => JoinStep.Waiting,
        SessionStep.StartingGame => JoinStep.Starting,
        SessionStep.InGame or SessionStep.GameClosed => JoinStep.InGame,
        _ => JoinStep.Lobby,
    };
}
