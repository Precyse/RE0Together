using CoopLauncher;
using CoopLauncher.Gui;
using CoopLauncher.Web;
using System.Text.Json.Nodes;
using System.Text.RegularExpressions;

namespace LauncherTests;

/// <summary>The snapshots the web window sends, worked out from fake statuses: the values of the hosting (H1), refused joining (D3),
/// lobby (L2), mini alert (F3) and guest select (S3) screens, the home and the lobbies tab, and the wire format.</summary>
public static class UiSnapshotTests
{
    private const ulong LobbyId = 109775243712345678;
    private const long UnixNow = 1_800_000_000;
    private const long NowMs = 100_000;

    private static readonly GameProfile Re0 = GameProfile.Load("re0");
    private static readonly GameProfile Sr2 = GameProfile.Load("sr2");
    private static readonly GameStatus Mod = new(new ModStatus(ModState.Installed, 412), true, 412, 17178773);
    private static readonly string[] Re0Characters = ["Rebecca", "Billy"];

    public static void Run()
    {
        var previous = Environment.GetEnvironmentVariable(AppData.DataRootVariable);
        var scratch = Path.Combine(Path.GetTempPath(), "coop-ui-snapshot-" + Guid.NewGuid().ToString("N"));
        Environment.SetEnvironmentVariable(AppData.DataRootVariable, scratch);   // the options a snapshot reads are the defaults
        try
        {
            Hosting();
            RefusedJoin();
            Lobby();
            MiniAlert();
            GuestSelect();
            Home();
            ModRuns();
            Lobbies();
            WireFormat();
        }
        finally
        {
            Environment.SetEnvironmentVariable(AppData.DataRootVariable, previous);
            if (Directory.Exists(scratch)) Directory.Delete(scratch, recursive: true);
        }
    }

    private static SnapshotInput Input(AppStatus status, Intent intent = Intent.None, JoinTarget? target = null, Tab tab = Tab.Play,
        PopupKind popup = PopupKind.None, ulong popupLobby = 0, Func<ulong, int?>? avatars = null, int newer = 0)
    {
        var games = new[] { Re0, Sr2 }.Select(profile => new GameEntry(profile, Mod, new ArtDto(null, null, null))).ToList();
        var screen = Screens.For(status, intent, target, tab);
        return new SnapshotInput(
            status, games, status.GameId ?? "re0", screen, null, tab, popup, popupLobby, LobbyFilter.All, null, target, GameCondition.Fine,
            new UpdateView(null, false, newer, false), [], "Build 412", 412, null, NowMs, UnixNow, _ => null, avatars ?? (_ => null));
    }

    private static PlayerSlot Host(bool local = false, SessionStep step = SessionStep.InGame) =>
        new(2, "Precyse", IsHost: true, IsLocal: local, Step: step, RttMs: local ? null : 18);

    private static PlayerSlot Guest(SessionStep step, bool local = false, bool held = false, int hold = 0) =>
        new(1, "Amy", IsHost: false, IsLocal: local, Step: step, RttMs: local ? null : 42, Held: held, HoldSecondsLeft: hold);

    private static AppStatus Re0Status(AppState state, SessionStep step, params PlayerSlot[] players) =>
        new(state, LobbyId) { GameId = "re0", Step = step, Seats = 2, SeatCharacters = Re0Characters, Bridged = true, Players = players };

    private static void Hosting()
    {
        var status = Re0Status(AppState.Hosting, SessionStep.Lobby, Host(local: true, step: SessionStep.Lobby)) with { LobbyOpenedAtMs = NowMs - 12_000 };
        var state = UiSnapshot.MainPage(Input(status, Intent.Host), ScreenKind.Home);
        Check.That(state.Screen == ScreenKind.Hosting, "H1: the hosting screen shows");
        Check.That(state.Transition == new TransitionDto(TransitionKind.PanelIn, 0), "H1: it opens over the tabs with the panel reveal");
        Check.That(state.Top.Mode == TopMode.Session && state.Top.Build == "Build 412" && state.Top.Update == null, "H1: the top bar is the session's");
        Check.That(state.Home == null && state.Lobbies == null && state.Joining == null && state.Select == null && state.Settings == null && state.Popup == null,
            "H1: only the screen on show has its DTO");
        var session = state.Session!;
        Check.That(session.Hosting && session.Code == "109775243712345678" && session.State == null, "H1: hosting with the code as text and no lobby state");
        Check.That(string.Join("/", session.Steps.Select(step => step.Text)) == "Lobby open/Mod 412/Waiting for players/In game", "H1: the hosting steps");
        Check.That(string.Join("", session.Steps.Select(step => step.Mark switch { StepMark.Done => 'D', StepMark.Active => 'A', _ => 'P' })) == "DDAP", "H1: lobby open and mod are done, waiting is active");
        Check.That(session.Steps[2].Trailing == "0:12", "H1: waiting for players counts from the lobby's opening");
        Check.That(!session.StartEnabled && session.Counter == "1/2" && session.Layout == CardLayout.Wide, "H1: start waits, two seats on the wide layout");
        Check.That(session.Seats.Length == 2 && session.Seats[0].Key == "0" && session.Seats[1].Key == "1", "H1: cards are keyed by seat");
        var host = session.Seats[0];
        Check.That(host.Local && host.Corner == "Host" && host.Occupant == "2" && host.Name == "Precyse" && host.Avatar.Initial == "P" && host.Avatar.Src == null && host.State == "idle",
            "H1: the host's own card says Host, names its player and has an initial before an avatar");
        Check.That(host.Figure?.Src?.StartsWith(WebHosts.ImageOrigin + "figure/re0/") == true && host.Right == string.Empty && host.Progress == null,
            "H1: the card's figure is a route on the image host");
        var open = session.Seats[1];
        Check.That(open.State == "open" && open.Name == "Open" && open.Occupant == string.Empty && open.Figure == null && open.Avatar.Initial == string.Empty, "H1: the second seat is open");
        var rail = state.Games;
        Check.That(rail.Length == 2 && rail[0].Id == "re0" && rail[0].Selected && !rail[0].Locked && rail[1].Locked && rail[0].Sub == "2 players" && rail[0].Line == "Mod 412",
            "H1: the rail locks the other game and says the selected game's line");
        Check.That(UiSnapshot.MainPage(Input(status, Intent.Host), ScreenKind.Hosting).Transition.Kind == TransitionKind.None, "H1: no transition while the screen stays");
    }

    private static void RefusedJoin()
    {
        var target = new JoinTarget(LobbyId, "Precyse", 2);
        var refusal = new Refusal(RefusalKind.ModFiles, RefusalVersions.Format(412, 409), "re0", LobbyId);
        var status = AppStatus.Idle with { GameId = null, Refusal = refusal };
        var state = UiSnapshot.MainPage(Input(status, Intent.Join, target, avatars: id => id == 2 ? 3 : null), ScreenKind.Home);
        Check.That(state.Screen == ScreenKind.Joining && state.Transition.Kind == TransitionKind.PanelIn, "D3: a refused join keeps the joining screen");
        var joining = state.Joining!;
        Check.That(joining.Title == "Joining Precyse", "D3: the title names the host");
        Check.That(joining.Host.Src == WebHosts.ImageOrigin + "avatar/2?r=3" && joining.Host.Initial == "P", "D3: the host's avatar route once the avatar arrived");
        Check.That(string.Join("", joining.Steps.Select(step => step.Mark switch { StepMark.Done => 'D', StepMark.Failed => 'F', _ => 'P' })) == "DFPPPP", "D3: the build check step failed");
        var failed = joining.Steps[1];
        Check.That(failed.Text == "Mod files differ" && failed.Pairs.Length == 2 && failed.Pairs[0].SequenceEqual(["Host", "412"]) && failed.Pairs[1].SequenceEqual(["You", "409"]),
            "D3: the failed row says why and gives the two builds");
        Check.That(joining.Fix == new BtnDto("UPDATE", true) && !joining.StartAgain, "D3: the fix is Update and usable");
        var withoutAvatar = UiSnapshot.MainPage(Input(status, Intent.Join, target), null).Joining!;
        Check.That(withoutAvatar.Host.Src == null, "D3: no avatar route before the avatar arrived");
        Check.That(UiSnapshot.MainPage(Input(status, Intent.Join, target), null).Transition.Kind == TransitionKind.None, "D3: the first snapshot has no transition");
    }

    private static void Lobby()
    {
        var status = new AppStatus(AppState.PeerConnected, LobbyId)
        {
            GameId = "sr2", Step = SessionStep.InGame, Seats = 4, Bridged = true, SessionStartedUnix = UnixNow - 3665,
            HostOptions = new Dictionary<string, string> { ["transport"] = "steam", ["max_players"] = "4" },
            Players = [Host(), Guest(SessionStep.InGame, local: true)],
        };
        var state = UiSnapshot.MainPage(Input(status, Intent.Join, new JoinTarget(LobbyId, "Precyse", 2)), ScreenKind.Joining);
        Check.That(state.Screen == ScreenKind.Lobby && state.Transition == new TransitionDto(TransitionKind.Page, 1), "L2: the lobby follows joining with the forward slide");
        var session = state.Session!;
        Check.That(!session.Hosting && session.State == new LobbyStateDto(LobbyStateKind.InGame, "In game", "1:01:05"), "L2: in game with the session timer");
        Check.That(session.Steps.Length == 0 && session.Layout == CardLayout.Tall && session.Seats.Length == 4 && !session.SeatsClickable, "L2: four seats on the tall layout, not clickable without figures");
        Check.That(session.Seats.Count(seat => seat.State == "open") == 2 && session.Seats[0].State == "ingame", "L2: two seats are open and the host's card is in game");
        Check.That(session.Options.Length >= 1 && session.Options.All(pair => pair.Length == 2), "L2: the host's options come as label and value pairs");
        Check.That(state.Top.Mode == TopMode.Session && state.Games[1].Selected && state.Games[1].Sub == "Running",
            "L2: the rail selects the lobby's game");
    }

    private static void MiniAlert()
    {
        var status = Re0Status(AppState.PeerConnected, SessionStep.InGame, Host(local: true), Guest(SessionStep.InGame, held: true, hold: 5));
        var mini = UiSnapshot.MiniPage(Input(status)).Mini;
        Check.That(mini.GameName == "RESIDENT EVIL 0" || mini.GameName == Re0.Name.ToUpperInvariant(), "F3: the mini window names the game in capitals");
        Check.That(mini.Header == new MiniHeaderDto(MiniHeaderKind.Lost, "PARTNER LOST") && mini.PlaySolo && mini.Layout == CardLayout.MiniPortrait, "F3: a lost partner, Play solo for the host, the portrait layout");
        var alert = mini.Alert!;
        Check.That(alert.Words == "Reconnecting" && alert.Card.State == "problem" && alert.Card.Name == "Amy" && alert.Card.Key == "1", "F3: the alert is the held partner's card");
        Check.That(alert.Remaining is { } share && Math.Abs(share - 0.5) < 1e-9 && alert.Holding.StartsWith("Holding ") && alert.Holding.EndsWith("0:05"), "F3: half of the grace is left");
        Check.That(mini.Seats.Length == 2 && mini.Seats[1].Right == "0:05", "F3: the held card's right text is the hold countdown");
        var calm = UiSnapshot.MiniPage(Input(Re0Status(AppState.PeerConnected, SessionStep.InGame, Host(local: true), Guest(SessionStep.InGame)))).Mini;
        Check.That(calm.Alert == null && calm.Header.Kind == MiniHeaderKind.Ping && calm.Header.Text == "42 ms" && !calm.PlaySolo, "F3: no alert, the ping in the header, no Play solo");
    }

    private static void GuestSelect()
    {
        var status = Re0Status(AppState.PeerConnected, SessionStep.Ready, Host(step: SessionStep.Ready), Guest(SessionStep.Ready, local: true));
        var input = Input(status) with { View = ScreenKind.Select, Screen = ScreenKind.Select };
        var state = UiSnapshot.MainPage(input, ScreenKind.Lobby);
        Check.That(state.Screen == ScreenKind.Select && state.Top.Mode == TopMode.View && state.Transition == new TransitionDto(TransitionKind.PanelIn, 0), "S3: the select view opens over the lobby");
        var select = state.Select!;
        Check.That(select.Centre == StageCentre.Lock && select.FocusHalf == 1, "S3: the lock sits between the halves and the guest's half has the focus");
        Check.That(select.Left.Kind == "readonly" && select.Left.Role == "Precyse Host" && !select.Left.CanCycle && select.Left.Dots == 0 && Math.Abs(select.Left.Opacity - 0.8) < 1e-6,
            "S3: the host's half is read-only");
        Check.That(select.Right.Kind == "own" && select.Right.Role == "You" && select.Right.Locked == false && select.Right.Opacity == 1.0 && select.Right.Name == "BILLY",
            "S3: the guest's own half is Billy and can still be changed");
        Check.That(select.Left.Figure.Src!.StartsWith(WebHosts.ImageOrigin + "figure/re0/") && select.Right.Figure.Initial == "B", "S3: figures are routes with their initial");
        var started = Re0Status(AppState.PeerConnected, SessionStep.StartingGame, Host(step: SessionStep.Ready), Guest(SessionStep.StartingGame, local: true));
        var locked = UiSnapshot.MainPage(Input(started) with { View = ScreenKind.Select, Screen = ScreenKind.Select }, null).Select!;
        Check.That(locked.Right.Locked && !locked.Right.CanCycle, "S3: the guest's outfit is locked once its game starts");
        Check.That(UiSnapshot.MainPage(input with { SelectFocus = 0 }, null).Select!.FocusHalf == 0, "S3: the controller's focus wins");
    }

    private static void Home()
    {
        var friend = new FriendLobby(2, "Precyse", LobbyId, "re0", new LobbyListing(LobbyId, "re0", 2, "Precyse", 1, 2, 412, 412, 17178773, new Dictionary<string, string>(), [], 0, 20));
        var status = AppStatus.Idle with { FriendLobbies = [friend] };
        var state = UiSnapshot.MainPage(Input(status, avatars: id => id == 2 ? 1 : null), null);
        var home = state.Home!;
        Check.That(state.Screen == ScreenKind.Home && home.HostLabel == "HOST" && !home.Native && home.HostEnabled && home.JoinEnabled && home.CodeEnabled && home.Mod == null,
            "home: an idle launcher with the game ready can host and join");
        Check.That(home.HasOptions == OptionsModel.Has(Re0) && home.HasCharacters && !home.OptionsOpen && home.Notice == null, "home: options, characters and no notice");
        Check.That(home.Friends.Length == 1 && home.Friends[0].Key == "109775243712345678" && home.Friends[0].Count == "1/2" && home.Friends[0].Primary
                   && home.Friends[0].Avatar.Src == WebHosts.ImageOrigin + "avatar/2?r=1", "home: a friend's card is keyed by the lobby and carries its avatar route");
        var missing = AppStatus.Idle with { Refusal = new Refusal(RefusalKind.ModNotInstalled, "", "re0", 0) };
        var notice = UiSnapshot.MainPage(Input(missing), null).Home!.Notice;
        Check.That(notice != null && notice.Words == "Mod not installed" && notice.Action == "Install" && notice.Report, "home: a refusal is a notice with its fix and Report");
        var options = UiSnapshot.MainPage(Input(AppStatus.Idle, popup: PopupKind.Options), null);
        Check.That(options.Popup is OptionsPopupDto { Kind: "options", ReadOnly: false } && options.Home!.OptionsOpen, "home: the options popup opens on the selected game");
        var native = AppStatus.Idle;
        var sr2Input = Input(native) with { SelectedId = "sr2" };
        var popup = (OptionsPopupDto)UiSnapshot.MainPage(sr2Input with { Popup = PopupKind.Options }, null).Popup!;
        Check.That(popup.GameName == Sr2.Name && popup.Rows.Any(row => row.Key == "transport" && row.Values.Length >= 2 && row.Value == row.Values[0].Value), "home: the option rows carry their values and the shown one");
    }

    private static IReadOnlyDictionary<string, ModRunView> Run(ModRunKind kind, ModProgress progress) =>
        new Dictionary<string, ModRunView> { ["re0"] = new(kind, progress) };

    /// <summary>A mod action on show: the step row on the home hero and the game's settings row, the locks while it runs, Done and Failed.</summary>
    private static void ModRuns()
    {
        var copying = Input(AppStatus.Idle) with { ModRuns = Run(ModRunKind.Install, new ModProgress(ModStep.Copying, 40, 12, 50)) };
        var home = UiSnapshot.MainPage(copying, null).Home!;
        Check.That(home.ModRun is { Failed: false, Retry: null, Step: { Text: "Copying 12/50", Mark: StepMark.Active, Trailing: "40%", Bar: 0.4 } }, "mod run: the step, file i of n, the percent and its bar");
        Check.That(!home.HostEnabled && !home.JoinEnabled && !home.CodeEnabled, "mod run: Host and Join wait while it runs");

        var settings = UiSnapshot.MainPage(copying with { Screen = ScreenKind.Settings, View = ScreenKind.Settings }, null).Settings!;
        var game = settings.Games.Single(g => g.Id == "re0");
        Check.That(game.ModRun?.Step.Trailing == "40%" && !game.BrowseEnabled && !game.ResetEnabled && !game.UninstallEnabled && settings.Games.Single(g => g.Id == "sr2").ModRun == null,
            "mod run: the game's settings row shows it and its buttons wait");

        var done = Input(AppStatus.Idle) with { ModRuns = Run(ModRunKind.Install, new ModProgress(ModStep.Done, 100, 50, 50)) };
        var doneHome = UiSnapshot.MainPage(done, null).Home!;
        Check.That(doneHome.ModRun is { Step: { Text: "Done", Mark: StepMark.Done, Trailing: "100%", Bar: 1.0 } } && doneHome.HostEnabled, "mod run: Done shows the check and frees Host");

        var failed = Input(AppStatus.Idle) with { ModRuns = Run(ModRunKind.Install, new ModProgress(ModStep.Failed, 37, 3, 50, "The file is in use")) };
        var failedHome = UiSnapshot.MainPage(failed, null).Home!;
        Check.That(failedHome.ModRun is { Failed: true, Retry: { Label: "Retry", Enabled: true }, Step: { Text: "The file is in use", Mark: StepMark.Failed, Trailing: "" } } && failedHome.HostEnabled,
            "mod run: Failed shows the reason in red with Retry");

        var wire = UiJson.Write(UiSnapshot.MainPage(copying, null));
        Check.That(wire.Contains("\"modRun\":{\"step\":{\"text\":\"Copying 12/50\",\"mark\":\"active\""), "mod run: the wire format names modRun and the step mark in camelCase");
        Check.That(ModRunViews.Text(new ModProgress(ModStep.Checking, 0, 0, 50)) == "Checking files" && ModRunViews.Text(new ModProgress(ModStep.RemovingOld, 91, 1, 1)) == "Removing old files",
            "mod run: the count is left out at the start of a step and for one file");
    }

    private static void Lobbies()
    {
        var listing = new LobbyListing(LobbyId, "sr2", 2, "Precyse", 2, 4, 412, 412, 17178773, new Dictionary<string, string> { ["transport"] = "steam" },
            [new RosterEntry("Precyse", SessionStep.InGame), new RosterEntry("Amy", SessionStep.CheckingBuild)], UnixNow - 125, 33);
        var status = AppStatus.Idle with { Lobbies = [listing] };
        var state = UiSnapshot.MainPage(Input(status, tab: Tab.Lobbies, popup: PopupKind.Lobby, popupLobby: LobbyId, avatars: id => id == 2 ? 4 : null), ScreenKind.Home);
        Check.That(state.Screen == ScreenKind.Lobbies && state.Transition == new TransitionDto(TransitionKind.Page, 1) && state.Top.Tab == Tab.Lobbies && state.Top.Mode == TopMode.Browse,
            "lobbies: the tab slides in from the right");
        var rows = state.Lobbies!.Rows;
        Check.That(rows.Length == 1 && rows[0].Key == "109775243712345678" && rows[0].GameName == Sr2.Name && rows[0].Players == "2/4" && rows[0].Ping == "33 ms"
                   && rows[0].Mod == new LobbyModDto("412", false) && rows[0].Action == new LobbyBtnDto("JOIN", true, true), "lobbies: a row has its columns and the primary JOIN");
        Check.That(rows[0].Host.Name == "Precyse" && rows[0].Host.Avatar.Src == WebHosts.ImageOrigin + "avatar/2?r=4", "lobbies: the host's avatar route");
        var popup = (LobbyPopupDto)state.Popup!;
        Check.That(popup.Caption == "PLAYERS 2/4" && popup.Roster.Length == 2 && popup.Roster[0].Host && popup.Roster[0].State == "ingame" && popup.Roster[1].State == "progress" && popup.OpenSeats == 2,
            "lobbies: the popup lists the roster by step and two open seats");
        Check.That(popup.Playing == "2:05" && popup.Network == "Steam" && popup.Ping == "33 ms" && popup.Mod == new CheckedDto(true, "412") && popup.Game == new CheckedDto(true, "Build 17178773"),
            "lobbies: the popup's playing time, network, ping, mod and game rows");
        Check.That(UiSnapshot.MainPage(Input(status, tab: Tab.Lobbies, popup: PopupKind.Lobby, popupLobby: 5), null).Popup == null, "lobbies: a popup for a lobby that is gone is not sent");
        Check.That(UiSnapshot.MainPage(Input(AppStatus.Idle, popup: PopupKind.Lobby, popupLobby: LobbyId), null).Popup == null, "lobbies: the lobby popup does not show over the home screen");
    }

    private static void WireFormat()
    {
        var friend = new FriendLobby(76561198000000001, "Precyse", LobbyId, "re0");
        var json = UiJson.Write(UiSnapshot.MainPage(Input(AppStatus.Idle with { FriendLobbies = [friend] }), null));
        Check.That(!Regex.IsMatch(json, @":\s*\d{16,}"), "wire: no 17-digit id is a number");
        var root = JsonNode.Parse(json)!.AsObject();
        Check.That(root["screen"]!.GetValue<string>() == "home" && root["transition"]!["kind"]!.GetValue<string>() == "none", "wire: enums are camelCase text");
        Check.That(root["top"]!["mode"]!.GetValue<string>() == "browse" && root["top"]!["tab"]!.GetValue<string>() == "play", "wire: top mode and tab");
        Check.That(root["games"]![0]!["tone"]!.GetValue<string>() == "quiet", "wire: the rail's tone is text");
        Check.That(root["session"] == null && root.ContainsKey("session") && root.ContainsKey("popup"), "wire: absent DTOs are present as null");
        Check.That(root["home"]!["friends"]![0]!["key"]!.GetValue<string>() == "109775243712345678", "wire: a lobby id is text");
        var message = UiMessages.State(7, json);
        var parsed = JsonNode.Parse(message)!.AsObject();
        Check.That(parsed["type"]!.GetValue<string>() == "state" && parsed["seq"]!.GetValue<int>() == 7 && parsed["screen"]!.GetValue<string>() == "home", "wire: a state message has type and seq in front of the snapshot");
        Check.That(JsonNode.Parse(UiMessages.Hello(PageKind.Mini, debug: false, reducedMotion: true))!["page"]!.GetValue<string>() == "mini", "wire: hello names the page");
        Check.That(JsonNode.Parse(UiMessages.Event(UiMessages.CodeRefused))!["name"]!.GetValue<string>() == "codeRefused", "wire: an event has its name");
    }
}
