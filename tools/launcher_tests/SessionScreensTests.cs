using CoopLauncher;
using CoopLauncher.Gui;

namespace LauncherTests;

/// <summary>The joining screen, the hosting screen and lobby, and the mini window worked out from an AppStatus.</summary>
public static class SessionScreensTests
{
    private const ulong LobbyId = 109775243712345678;
    private const long Now = 100_000;

    private static readonly string[] Re0Characters = ["Rebecca", "Billy"];
    private static readonly GameProfile Re0 = GameProfile.Load("re0");
    private static readonly GameProfile Sr2 = GameProfile.Load("sr2");
    private static readonly GameStatus Mod = new(new ModStatus(ModState.Installed, 412), true, 412, 17178773);

    private static PlayerSlot HostSlot(SessionStep step = SessionStep.InGame, bool local = false, string options = "", int? rtt = 18) =>
        new(2, "Precyse", IsHost: true, IsLocal: local, Step: step, RttMs: local ? null : rtt, Options: options);

    private static PlayerSlot GuestSlot(SessionStep step, bool local = false, ulong id = 1, string name = "Amy", int percent = 0, int? rtt = 42,
        bool held = false, bool silent = false, int hold = 0, string options = "") =>
        new(id, name, IsHost: false, IsLocal: local, Step: step, Percent: percent, RttMs: local ? null : rtt, Held: held, Silent: silent,
            Options: options, HoldSecondsLeft: hold);

    private static AppStatus Re0Status(SessionStep step, params PlayerSlot[] players) =>
        new(AppState.Joined, LobbyId) { GameId = "re0", Step = step, Seats = 2, SeatCharacters = Re0Characters, Bridged = true, Players = players };

    private static AppStatus Sr2Status(SessionStep step, params PlayerSlot[] players) =>
        new(AppState.Joined, LobbyId)
        {
            GameId = "sr2", Step = step, Seats = 4, Bridged = true, Players = players,
            HostOptions = new Dictionary<string, string> { ["transport"] = "steam", ["max_players"] = "4" },
        };

    public static void Run()
    {
        JoiningSteps();
        JoiningRefusals();
        JoiningHeader();
        StartAgain();
        Hosting();
        Lobby();
        Seats();
        Mini();
    }

    private static JoiningModel Join(AppStatus status, GameProfile? game = null, JoinTarget? target = null, GameCondition condition = GameCondition.Fine,
        int newer = 0, long now = Now) =>
        SessionScreens.Joining(status, game ?? Re0, Mod, target, condition, newer, now);

    private static string Marks(JoiningModel model) =>
        string.Concat(model.Steps.Select(row => row.Mark switch { StepMark.Done => 'D', StepMark.Active => 'A', StepMark.Failed => 'F', _ => 'P' }));

    private static void JoiningSteps()
    {
        var guest = new[] { HostSlot(), GuestSlot(SessionStep.None, local: true) };
        var rows = Join(Re0Status(SessionStep.Lobby, guest)).Steps;
        Check.That(string.Join("/", rows.Select(row => row.Text)) == "Lobby/Build check/Receiving save/Waiting for Precyse/Starting game/In game",
            "joining: re0 steps are lobby, build check, save, waiting for the host, starting, in game");
        Check.That(Marks(Join(Re0Status(SessionStep.Lobby, guest))) == "APPPPP", "joining: in the lobby the first step is active");
        Check.That(Marks(Join(Re0Status(SessionStep.CheckingBuild, guest))) == "DAPPPP", "joining: checking the build");
        Check.That(Marks(Join(Re0Status(SessionStep.Ready, guest))) == "DDDAPP", "joining: waiting for the host is the step Ready");
        Check.That(Marks(Join(Re0Status(SessionStep.StartingGame, guest))) == "DDDDAP", "joining: starting the game");
        Check.That(Marks(Join(Re0Status(SessionStep.InGame, guest))) == "DDDDDD", "joining: in game everything is done");
        Check.That(Marks(Join(Re0Status(SessionStep.None, guest))) == "APPPPP", "joining: no step yet is the lobby step");

        var sr2 = Join(Sr2Status(SessionStep.Ready, HostSlot(), GuestSlot(SessionStep.None, local: true)), Sr2);
        Check.That(string.Join("/", sr2.Steps.Select(row => row.Text)) == "Lobby/Build check/Waiting for Precyse/Starting game/In game",
            "joining: a game without save sync has no save step");
        var unbridged = Sr2Status(SessionStep.Lobby, HostSlot()) with { HostOptions = new Dictionary<string, string> { ["transport"] = "native" } };
        Check.That(!Join(unbridged, Sr2).Steps.Any(row => row.Text == "Build check"), "joining: no build check without the bridge");

        var receiving = Re0Status(SessionStep.ReceivingSave, guest) with
        {
            SavePercent = 40, SaveFile = "data0.bin", SaveBytes = 943718, SaveTotal = 2411725,
        };
        var save = Join(receiving).Steps[2];
        Check.That(save.Mark == StepMark.Active && save.Trailing == "40%" && save.Bar is { } bar && Math.Abs(bar - 0.4) < 1e-9, "joining: the save row has its percent and bar");
        Check.That(save.Lines is { Count: 1 } && save.Lines[0] == "data0.bin  0.9 / 2.3 MB", "joining: the file line has one decimal of MB");
        Check.That(Join(receiving with { SaveFile = "" }).Steps[2].Lines == null, "joining: no file line before a file is known");

        var starting = Re0Status(SessionStep.StartingGame, guest) with { StepSinceMs = Now - 12_000 };
        var row = Join(starting).Steps[4];
        Check.That(row.Trailing == "0:12" && row.Indeterminate && row.Bar == null, "joining: starting has a timer and the indeterminate bar");
        Check.That(Join(starting with { StepSinceMs = null }).Steps[4].Trailing == "", "joining: no timer without a start time");
    }

    private static void JoiningRefusals()
    {
        var guest = new[] { HostSlot(), GuestSlot(SessionStep.Refused, local: true) };
        var refused = Re0Status(SessionStep.Refused, guest) with { Refusal = new Refusal(RefusalKind.ModFiles, RefusalVersions.Format(412, 409), "re0", LobbyId) };
        var model = Join(refused, newer: 0);
        Check.That(Marks(model) == "DFPPPP", "refusal: mod files fail the build check, later steps stay pending");
        Check.That(model.Steps[1].Text == "Mod files differ", "refusal: the failed row says the refusal's words");
        Check.That(model.Steps[1].Pairs is { Count: 2 } pairs && pairs[0] == ("Host", "412") && pairs[1] == ("You", "409"), "refusal: the host and you versions");
        Check.That(model.Fix == NoticeFix.UpdateMod && model.FixLabel == "UPDATE" && model.FixEnabled, "refusal: the fix and its upper-case label");

        var launcher = Join(Re0Status(SessionStep.Refused, guest) with { Refusal = new Refusal(RefusalKind.LauncherBuild, RefusalVersions.Format(412, 409), "re0") }, newer: 412);
        Check.That(launcher.Fix == NoticeFix.UpdateLauncher && launcher.FixLabel == "UPDATE TO 412", "refusal: update to the host's build");

        Check.That(Marks(Join(Re0Status(SessionStep.Refused, guest) with { Refusal = new Refusal(RefusalKind.SaveTimedOut, "", "re0", LobbyId) })) == "DDFPPP",
            "refusal: a save that timed out fails the save step");
        Check.That(Marks(Join(Re0Status(SessionStep.Refused, guest) with { Refusal = new Refusal(RefusalKind.LaunchFailed, "", "re0") })) == "DDDDFP",
            "refusal: a launch that failed fails starting");
        var full = Join(Re0Status(SessionStep.None) with { Refusal = new Refusal(RefusalKind.LobbyFull, "", "re0", LobbyId) });
        Check.That(Marks(full) == "FPPPPP" && full.Fix == NoticeFix.JoinAgain && full.FixLabel == "JOIN AGAIN", "refusal: a full lobby fails the lobby step and offers Join again");
        Check.That(full.Steps[0].Pairs == null, "refusal: no versions for a refusal without them");

        var missing = SessionScreens.Joining(Re0Status(SessionStep.Refused, guest) with { Refusal = new Refusal(RefusalKind.ModNotInstalled, "", "re0") }, Re0,
            new GameStatus(new ModStatus(ModState.NotInstalled, 0), true, 412, 17178773, GameRunning: true), null, GameCondition.Fine, 0, Now);
        Check.That(missing.Fix == NoticeFix.InstallMod && !missing.FixEnabled, "refusal: Install is not usable while the game runs");
        Check.That(Join(Re0Status(SessionStep.Lobby, guest) with { Invite = new PendingInvite(9, 9, "Zed") }).Fix == NoticeFix.None, "refusal: an invite is not a fix of the joining screen");
    }

    private static void JoiningHeader()
    {
        var guest = new[] { HostSlot(), GuestSlot(SessionStep.Lobby, local: true) };
        var model = Join(Re0Status(SessionStep.Lobby, guest));
        Check.That(model.Title == "Joining Precyse" && model.Sub == "Billy" && model.HostId == 2, "joining: the host, and the character of the seat");
        var four = Join(Sr2Status(SessionStep.Lobby, HostSlot(), GuestSlot(SessionStep.Lobby, id: 3, name: "Zed"), GuestSlot(SessionStep.Lobby, id: 4, name: "Kai"),
            GuestSlot(SessionStep.Lobby, local: true)), Sr2);
        Check.That(four.Sub == "Player 4 of 4", "joining: a game without characters says the seat's number");
        var typed = Join(new AppStatus(AppState.Connecting));
        Check.That(typed.Title == "Joining" && typed.Sub == "" && typed.HostId == 0, "joining: a typed code is joining nobody yet");
        Check.That(typed.Steps[3].Text == "Waiting for host", "joining: the waiting row names the host once known");
        var target = Join(new AppStatus(AppState.Connecting), target: new JoinTarget(LobbyId, "Mika", 77));
        Check.That(target.Title == "Joining Mika" && target.HostId == 77, "joining: the target names the host before the lobby does");
    }

    private static void StartAgain()
    {
        var starting = Re0Status(SessionStep.StartingGame, HostSlot(), GuestSlot(SessionStep.StartingGame, local: true)) with { StepSinceMs = 1_000 };
        Check.That(!Join(starting, now: 10_999).StartAgain && Join(starting, now: 11_000).StartAgain, "start again: offered once starting lasted 10 s");
        Check.That(Join(starting, condition: GameCondition.NotStarted, now: 2_000).StartAgain, "start again: the game did not start");
        Check.That(Join(starting, condition: GameCondition.Exited, now: 2_000).StartAgain, "start again: the game exited");
        Check.That(!Join(starting with { Step = SessionStep.ReceivingSave }, now: 50_000).StartAgain, "start again: not before the game is starting");
    }

    private static SessionModel Host(AppStatus status, GameProfile? game = null, long now = Now, IReadOnlyList<string>? events = null) =>
        SessionScreens.Session(status, game ?? Re0, null, Mod, GameCondition.Fine, events ?? [], now, unixNow: 5_000);

    private static void Hosting()
    {
        var alone = Re0Status(SessionStep.Ready, HostSlot(SessionStep.Ready, local: true)) with { State = AppState.Hosting, LobbyOpenedAtMs = Now - 42_000 };
        var model = Host(alone);
        Check.That(model.Hosting && model.Code == "109775243712345678", "hosting: the host's screen with the code");
        Check.That(string.Join("/", model.Steps.Select(row => row.Text)) == "Lobby open/Mod 412/Waiting for players/In game", "hosting: the steps");
        Check.That(model.Steps[0].Mark == StepMark.Done && model.Steps[1].Mark == StepMark.Done && model.Steps[2].Mark == StepMark.Active && model.Steps[3].Mark == StepMark.Pending,
            "hosting: the lobby and the mod are done, waiting is active");
        Check.That(model.Steps[2].Trailing == "0:42", "hosting: the waiting timer counts from the lobby opening");
        Check.That(model.Counter == "1/2" && model.Layout == CardLayout.Wide, "hosting: two seats are wide cards under a PLAYERS count");
        Check.That(model.StartEnabled, "hosting: alone and ready, START GAME can be used");

        var opening = Host(Re0Status(SessionStep.None, HostSlot(SessionStep.None, local: true)) with { State = AppState.Connecting, LobbyId = 0 });
        Check.That(opening.Steps[0].Mark == StepMark.Active && opening.Steps[1].Mark == StepMark.Pending && !opening.StartEnabled, "hosting: while the lobby opens nothing is done");

        var joining = new[] { HostSlot(SessionStep.Ready, local: true), GuestSlot(SessionStep.ReceivingSave, percent: 40) };
        var sending = Host(Re0Status(SessionStep.Ready, joining));
        Check.That(sending.Steps[2].Text == "Sending save to Amy" && sending.Steps[2].Mark == StepMark.Active && !sending.StartEnabled, "hosting: sending the save, START GAME waits");
        foreach (var step in new[] { SessionStep.Lobby, SessionStep.CheckingBuild })
            Check.That(!Host(Re0Status(SessionStep.Ready, HostSlot(SessionStep.Ready, local: true), GuestSlot(step))).StartEnabled, $"hosting: START GAME waits for a guest in {step}");
        Check.That(Host(Re0Status(SessionStep.Ready, HostSlot(SessionStep.Ready, local: true), GuestSlot(SessionStep.Ready))).StartEnabled, "hosting: every guest ready enables START GAME");
        Check.That(!Host(Re0Status(SessionStep.Lobby, HostSlot(SessionStep.Lobby, local: true))).StartEnabled, "hosting: START GAME needs the host's step Ready");

        var starting = Host(Re0Status(SessionStep.StartingGame, HostSlot(SessionStep.StartingGame, local: true), GuestSlot(SessionStep.Ready)) with { StepSinceMs = Now - 3_000 });
        Check.That(starting.Steps[2].Text == "Starting game" && starting.Steps[2].Trailing == "0:03" && !starting.StartEnabled, "hosting: after START the third row is starting");

        var four = Host(Sr2Status(SessionStep.Ready, HostSlot(SessionStep.Ready, local: true), GuestSlot(SessionStep.Ready, id: 3), GuestSlot(SessionStep.Ready, id: 4),
            GuestSlot(SessionStep.Ready, id: 5)), Sr2);
        Check.That(four.Counter == "4/4" && four.Layout == CardLayout.Tall && four.StartEnabled, "hosting: four seats are tall cards");
        Check.That(Host(Re0Status(SessionStep.InGame, HostSlot(SessionStep.InGame, local: true))).Hosting == false, "hosting: once the host is in game it is the lobby");
    }

    private static void Lobby()
    {
        var players = new[] { HostSlot(), GuestSlot(SessionStep.InGame, local: true) };
        var status = Re0Status(SessionStep.InGame, players) with
        {
            State = AppState.PeerConnected, SessionStartedUnix = 5_000 - 3_725,
            HostOptions = new Dictionary<string, string> { ["partner_mode"] = "state", ["character"] = "rebecca" },
        };
        var model = Host(status, events: ["a", "b", "c"]);
        Check.That(!model.Hosting && model.Layout == CardLayout.Tall && model.Counter == "", "lobby: tall cards and no PLAYERS caption");
        Check.That(model.State is { Kind: LobbyStateKind.InGame, Words: "In game", Timer: "1:02:05" }, "lobby: in game with the session timer");
        Check.That(model.Options.Count == 1 && model.Options[0] == ("Partner mode", "State"), "lobby: the host's options, not the character");
        Check.That(model.Events.SequenceEqual(["b", "c"]), "lobby: the last two events");
        Check.That(!model.Relaunch && !model.StartEnabled, "lobby: no relaunch while the game runs");
        Check.That(Host(status with { SessionStartedUnix = 0 }).State!.Timer == "", "lobby: no timer before the start of play is known");

        var hostLocal = Host(Re0Status(SessionStep.InGame, HostSlot(SessionStep.InGame, local: true), GuestSlot(SessionStep.InGame)));
        Check.That(!hostLocal.Hosting, "lobby: the host in game is in the lobby");

        var rejoining = Host(new AppStatus(AppState.Rejoining, LobbyId) { GameId = "re0", SecondsLeft = 67 });
        Check.That(rejoining.State is { Kind: LobbyStateKind.Reconnecting, Words: "Reconnecting", Timer: "1:07" } && rejoining.Seats.Count == 0, "lobby: reconnecting counts the seconds left");
        var waiting = Host(new AppStatus(AppState.WaitingForHost, LobbyId) { GameId = "re0", SecondsLeft = 161 });
        Check.That(waiting.State is { Kind: LobbyStateKind.WaitingForHost, Words: "Waiting for host", Timer: "2:41" }, "lobby: waiting for the host");

        var closed = SessionScreens.Session(status, Re0, null, Mod, GameCondition.Exited, [], Now, 5_000);
        Check.That(closed.State!.Kind == LobbyStateKind.GameClosed && closed.Relaunch, "lobby: the game exited, relaunch");
        Check.That(Host(status with { Invite = new PendingInvite(55, 9, "Amy") }).Notice is { Words: "Invite from Amy", Action: "Switch" }, "lobby: an invite is the notice");
        Check.That(Host(status).Notice == null, "lobby: no notice without an invite");
    }

    private static void Seats()
    {
        var players = new[]
        {
            HostSlot(SessionStep.Ready, local: true, options: "outfit_rebecca=02"),
            GuestSlot(SessionStep.ReceivingSave, percent: 40, options: "outfit_billy=95"),
        };
        var wide = Host(Re0Status(SessionStep.Ready, players)).Seats;
        Check.That(wide[0].Name == "Precyse" && wide[0].Corner == "Host" && wide[0].Footer == "" && wide[0].Local, "seats: the hosting screen's own host card says Host in its corner");
        Check.That(wide[0].Detail == "Rebecca  S.T.A.R.S. 2002" && wide[0].Figure is { Character: "rebecca", OptionKey: "outfit_rebecca", Value: "02", Initial: "R" },
            "seats: the character and the outfit, and the figure");
        Check.That(wide[1].Corner == "42 ms" && wide[1].Detail == "Billy  Jacket" && wide[1].State == CardState.Progress, "seats: ping, outfit and state of a guest");
        Check.That(wide[1].Steps.Count == 2 && wide[1].Steps[0].Mark == StepMark.Done && wide[1].Steps[1].Trailing == "40%" && wide[1].Progress is { } share && Math.Abs(share - 0.4) < 1e-9,
            "seats: a receiving guest shows the build check done and the save with its percent");

        var open = Host(Re0Status(SessionStep.Ready, HostSlot(SessionStep.Ready, local: true))).Seats;
        Check.That(open.Count == 2 && open[1].State == CardState.Open && open[1].Name == "Open" && open[1].Detail == "Billy", "seats: an open seat names its character");

        var lobby = Host(Re0Status(SessionStep.InGame, HostSlot(options: "outfit_rebecca=02"), GuestSlot(SessionStep.InGame, local: true, options: "outfit_billy=a5"))).Seats;
        Check.That(lobby[0].Corner == "18 ms" && lobby[0].Footer == "Host" && !lobby[0].Local, "seats: a remote host shows its ping and Host in the footer");
        Check.That(lobby[1].Corner == "You" && lobby[1].Local && lobby[1].Words == "In game" && lobby[1].State == CardState.InGame, "seats: the local card reads You");

        var held = Host(Re0Status(SessionStep.InGame, HostSlot(local: true), GuestSlot(SessionStep.InGame, held: true, hold: 7))).Seats[1];
        Check.That(held.Corner == "0:07" && held.Countdown == "0:07" && held.State == CardState.Problem && held.Words == "Reconnecting", "seats: a held partner shows the countdown");

        var sr2 = Host(Sr2Status(SessionStep.InGame, HostSlot(), GuestSlot(SessionStep.InGame, local: true, id: 3), GuestSlot(SessionStep.ReceivingSave, id: 4, percent: 40)), Sr2).Seats;
        Check.That(sr2.Count == 4 && sr2[3].State == CardState.Open && sr2[3].Detail == "" && sr2.All(card => card.Figure == null), "seats: open seats up to the seat count, no figures without a character select");
        Check.That(sr2[2].Steps.Count == 0 && sr2[2].Progress is { } p && Math.Abs(p - 0.4) < 1e-9, "seats: four seats show the percent, not the step list");
        Check.That(sr2[0].Footer == "Host" && sr2[0].Name == "Precyse", "seats: the host first");
    }

    private static void Mini()
    {
        var host = HostSlot(SessionStep.InGame, local: true);
        var calm = SessionScreens.Mini(Re0Status(SessionStep.InGame, host, GuestSlot(SessionStep.InGame, rtt: 18)), Re0, null, ["Partner joined"]);
        Check.That(calm.Header == new MiniHeader(MiniHeaderKind.Ping, "18 ms") && calm.Layout == CardLayout.MiniPortrait && calm.Alert == null && !calm.PlaySolo, "mini: the ping, portrait cards for re0");
        Check.That(calm.Event == "Partner joined" && calm.GameName == "RESIDENT EVIL 0 HD", "mini: the game and the newest event");
        Check.That(calm.Seats[1].Detail == "Billy", "mini: the character under the name");

        var many = SessionScreens.Mini(Sr2Status(SessionStep.InGame, HostSlot(SessionStep.InGame, local: true), GuestSlot(SessionStep.InGame, id: 3, rtt: 63), GuestSlot(SessionStep.InGame, id: 4, rtt: 58)), Sr2, null, []);
        Check.That(many.Header.Text == "63 ms" && many.Layout == CardLayout.Mini && many.Seats.Count == 4 && many.Event == "", "mini: the highest partner ping, four compact cards");

        var joining = SessionScreens.Mini(Sr2Status(SessionStep.InGame, host, GuestSlot(SessionStep.ReceivingSave, id: 3, name: "Kai", percent: 40)), Sr2, null, []);
        Check.That(joining.Header == new MiniHeader(MiniHeaderKind.Joining, "KAI JOINING") && joining.Seats[1].Progress is { } share && Math.Abs(share - 0.4) < 1e-9, "mini: a partner joining, with its percent");

        var held = Re0Status(SessionStep.InGame, host, GuestSlot(SessionStep.InGame, held: true, hold: 7));
        var alert = SessionScreens.Mini(held, Re0, null, []);
        Check.That(alert.Header == new MiniHeader(MiniHeaderKind.Lost, "PARTNER LOST") && alert.PlaySolo, "mini: partner lost, the host may play solo");
        Check.That(alert.Alert is { Words: "Reconnecting", Holding: "Holding Billy 0:07" } a && a.Remaining is { } left && Math.Abs(left - 0.7) < 1e-9 && a.Card.Name == "Amy",
            "mini: the alert names the partner, the character and the time left");
        var guestView = Re0Status(SessionStep.InGame, HostSlot(local: false) with { Held = true, HoldSecondsLeft = 90 }, GuestSlot(SessionStep.InGame, local: true));
        var guestAlert = SessionScreens.Mini(guestView, Re0, null, []);
        Check.That(!guestAlert.PlaySolo && guestAlert.Alert is { Remaining: { } host90 } && Math.Abs(host90 - 0.5) < 1e-9, "mini: a guest cannot play solo, a lost host's grace is longer");
        var silent = SessionScreens.Mini(Sr2Status(SessionStep.InGame, host, GuestSlot(SessionStep.InGame, id: 3, silent: true)), Sr2, null, []);
        Check.That(silent.Alert is { Words: "Not responding", Remaining: null, Holding: "" }, "mini: a silent partner has no countdown");
    }
}
