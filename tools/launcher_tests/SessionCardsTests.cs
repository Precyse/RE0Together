using CoopLauncher;
using CoopLauncher.Gui;

namespace LauncherTests;

/// <summary>The seats, the friends, the notice and the Session view worked out from an AppStatus.</summary>
public static class SessionCardsTests
{
    private const ulong LobbyId = 109775243712345678;
    private const int NewerBuild = 413;

    private static readonly string[] Re0Characters = ["Rebecca", "Billy"];

    private static readonly GameProfile Re0 = GameProfile.Load("re0");
    private static readonly GameProfile Sr2 = GameProfile.Load("sr2");

    private static PlayerSlot Host(SessionStep step = SessionStep.InGame) =>
        new(1, "Precyse", IsHost: true, IsLocal: true, Step: step);

    private static PlayerSlot Guest(SessionStep step = SessionStep.InGame, int percent = 0, int? rtt = 42, ulong id = 2, bool held = false) =>
        new(id, "Partner", IsHost: false, IsLocal: false, Step: step, Percent: percent, RttMs: rtt, Held: held);

    private static AppStatus InLobby(params PlayerSlot[] players) =>
        new(AppState.PeerConnected, LobbyId, 42) { GameId = "re0", Players = players };

    private static GameStatus Mod(ModState state) => new(new ModStatus(state, 412), true, 412, 17178773);

    public static void Run()
    {
        Seats();
        Friends();
        Fixes();
        Notices();
        Resolution();
        Views();
    }

    private static void Seats()
    {
        var two = SessionCards.Seats(InLobby(Guest(), Host()), Re0Characters, 2);
        Check.That(two.Count == 2 && two[0].Name == "Precyse" && two[1].Name == "Partner", "seats: the host first");
        Check.That(two[0].Role == "Host / You" && two[1].Role == "Guest", "seats: roles, the local seat reads You");
        Check.That(two[0].Detail == "Rebecca" && two[1].Detail == "Billy", "seats: characters in seat order");
        Check.That(two[0].Ping == "" && two[1].Ping == "42 ms", "seats: a ping on remote seats only");
        Check.That(two[0].AvatarId == 1 && two[1].AvatarId == 2, "seats: the avatar is the player's id");
        Check.That(two[0].Status == "In game" && two[0].Lamp == Theme.Live, "seats: status and lamp come from StatusText");

        var receiving = SessionCards.Seats(InLobby(Host(), Guest(SessionStep.ReceivingSave, percent: 40)), Re0Characters, 2);
        Check.That(receiving[1].Progress is { } share && Math.Abs(share - 0.4) < 1e-9 && receiving[0].Progress == null, "seats: progress only while receiving the save");

        var held = SessionCards.Seats(InLobby(Host(), Guest(held: true)), Re0Characters, 2);
        Check.That(held[1].Status == "Reconnecting" && held[1].Lamp == Theme.Armed, "seats: a held seat reads Reconnecting");

        var open = SessionCards.Seats(InLobby(Host()), [], 4);
        Check.That(open.Count == 4 && open.Skip(1).All(card => card.Open && card.Name == "Open" && card.AvatarId == 0 && card.Lamp == Theme.Dim),
            "seats: open seats up to the seat count");
        Check.That(open[1].Detail == "" && open[1].Status == "", "seats: no character, no status on an open seat of a game without characters");

        var openRe0 = SessionCards.Seats(AppStatus.Idle, Re0Characters, 2);
        Check.That(openRe0.All(card => card.Open) && openRe0[0].Detail == "Rebecca" && openRe0[1].Detail == "Billy", "seats: idle shows open seats with their characters");

        var crowded = SessionCards.Seats(InLobby(Host(), Guest(id: 2), Guest(id: 3), Guest(id: 4)), [], 2);
        Check.That(crowded.Count == 4 && crowded.All(card => !card.Open), "seats: never fewer than the members");

        var threeOfFour = SessionCards.Seats(InLobby(Host(), Guest(id: 2), Guest(id: 3)), [], GameOptions.Seats(Sr2, new Dictionary<string, string> { ["max_players"] = "3" }));
        Check.That(threeOfFour.Count == 3, "seats: the host's max_players sets the seat count (SR2)");
        var fourSeats = SessionCards.Seats(InLobby(Host(), Guest(id: 2), Guest(id: 3)), [], GameOptions.Seats(Sr2, new Dictionary<string, string>()));
        Check.That(fourSeats.Count == 4 && fourSeats[3].Open, "seats: without a host choice the profile's maximum (SR2: four, one open)");
    }

    private static void Friends()
    {
        var status = AppStatus.Idle with
        {
            FriendLobbies = [new FriendLobby(7, "Zed", 71, "sr2"), new FriendLobby(8, "Amy", 81, "re0")],
        };
        var cards = SessionCards.Friends(status, [Re0, Sr2]);
        Check.That(cards.Count == 2 && cards[0] == new FriendCard(7, "Zed", Sr2.Name, 71) && cards[1] == new FriendCard(8, "Amy", Re0.Name, 81),
            "friends: the app's order, with the game's name");
    }

    private static void Fixes()
    {
        static NoticeFix Fix(RefusalKind kind, ulong lobby = 0, int newer = 0) =>
            SessionCards.FixFor(AppStatus.Idle with { Refusal = new Refusal(kind, LobbyId: lobby) }, newer);

        Check.That(Fix(RefusalKind.ModNotInstalled) == NoticeFix.InstallMod, "fix: mod not installed installs");
        Check.That(Fix(RefusalKind.ModFiles) == NoticeFix.UpdateMod, "fix: mod files differ updates");
        Check.That(Fix(RefusalKind.LauncherBuild, newer: NewerBuild) == NoticeFix.UpdateLauncher && Fix(RefusalKind.UnknownGame, newer: NewerBuild) == NoticeFix.UpdateLauncher,
            "fix: launcher build and unknown game update the launcher when a newer one is known");
        Check.That(Fix(RefusalKind.LauncherBuild) == NoticeFix.None && Fix(RefusalKind.UnknownGame) == NoticeFix.None, "fix: none without a newer release");
        Check.That(Fix(RefusalKind.GameNotFound) == NoticeFix.Browse, "fix: game not found browses");
        foreach (var kind in new[] { RefusalKind.LobbyFull, RefusalKind.LobbyClosed, RefusalKind.LobbyNotAllowed, RefusalKind.LobbyFailed,
                     RefusalKind.NotCoopLobby, RefusalKind.SteamOffline, RefusalKind.SaveTimedOut, RefusalKind.RejoinGaveUp })
        {
            Check.That(Fix(kind, LobbyId) == NoticeFix.JoinAgain, $"fix: {kind} joins again when the refusal carries a lobby");
            Check.That(Fix(kind) == NoticeFix.None, $"fix: {kind} has no fix without a lobby");
        }
        foreach (var kind in new[] { RefusalKind.GameBuild, RefusalKind.GameUnsupported, RefusalKind.RequirementMissing, RefusalKind.SaveFolderMissing,
                     RefusalKind.HostLeft, RefusalKind.PortInUse, RefusalKind.LaunchFailed })
            Check.That(Fix(kind, LobbyId, NewerBuild) == NoticeFix.None, $"fix: {kind} has none");
    }

    private static void Notices()
    {
        Check.That(SessionCards.NoticeFor(AppStatus.Idle, 0) == null, "notice: none for a quiet status");

        var refused = AppStatus.Idle with { Refusal = new Refusal(RefusalKind.ModFiles, GameId: "re0") };
        var notice = SessionCards.NoticeFor(refused, 0);
        Check.That(notice is { Words: "Mod files differ", Action: "Update", Report: true }, "notice: a refusal has its words, its fix and Report");
        Check.That(SessionCards.NoticeFor(refused with { Refusal = new Refusal(RefusalKind.ModFiles, GameId: "sr2") }, 0)!.Key != notice!.Key, "notice: the key tells games apart");

        var launcher = SessionCards.NoticeFor(AppStatus.Idle with { Refusal = new Refusal(RefusalKind.LauncherBuild) }, NewerBuild);
        Check.That(launcher is { Words: "Launcher build differs", Action: "Update to 413" }, "notice: update names the newer build");
        Check.That(SessionCards.NoticeFor(AppStatus.Idle with { Refusal = new Refusal(RefusalKind.LobbyFull, LobbyId: LobbyId) }, 0) is { Action: "Join again" }, "notice: join again");
        Check.That(SessionCards.NoticeFor(AppStatus.Idle with { Refusal = new Refusal(RefusalKind.GameNotFound, GameId: "re0") }, 0) is { Action: "Browse" }, "notice: browse");
        Check.That(SessionCards.NoticeFor(AppStatus.Idle with { Refusal = new Refusal(RefusalKind.ModNotInstalled, GameId: "re0") }, 0) is { Action: "Install" }, "notice: install");
        Check.That(SessionCards.NoticeFor(AppStatus.Idle with { Refusal = new Refusal(RefusalKind.HostLeft) }, 0) is { Action: null, Report: true }, "notice: no fix, still Report");

        var invited = InLobby(Host()) with { Invite = new PendingInvite(55, 9, "Amy") };
        Check.That(SessionCards.NoticeFor(invited, 0) is { Words: "Invite from Amy", Action: "Switch", Report: false }, "notice: an invite offers Switch, no Report");
        Check.That(SessionCards.NoticeFor(invited with { Refusal = new Refusal(RefusalKind.HostLeft) }, 0) is { Words: "Host left" }, "notice: a refusal wins over an invite");
    }

    private static void Resolution()
    {
        Check.That(SessionCards.Resolved(new Refusal(RefusalKind.ModNotInstalled), Mod(ModState.Installed)), "resolved: the mod was installed");
        Check.That(!SessionCards.Resolved(new Refusal(RefusalKind.ModNotInstalled), Mod(ModState.NotInstalled)), "resolved: not while the mod is missing");
        Check.That(SessionCards.Resolved(new Refusal(RefusalKind.GameNotFound), Mod(ModState.Installed)), "resolved: the game was found");
        Check.That(!SessionCards.Resolved(new Refusal(RefusalKind.ModFiles), Mod(ModState.Installed)), "resolved: differing files are not fixed by an installed mod");
        Check.That(!SessionCards.Resolved(new Refusal(RefusalKind.LobbyFull), Mod(ModState.Installed)), "resolved: a lobby refusal stays");
    }

    private static SessionView View(AppStatus status, GameStatus? mod = null, bool locked = false, bool relaunch = false) =>
        SessionCards.View(status, Re0, mod ?? Mod(ModState.Installed), locked, relaunch, 0, [Re0, Sr2]);

    private static void Views()
    {
        var idle = View(AppStatus.Idle);
        Check.That(idle is { Title: "Session", InLobby: false, HostEnabled: true, JoinEnabled: true, CodeEnabled: true, Mod: null }, "view: idle with the mod installed");
        Check.That(!idle.LeaveEnabled && !idle.CopyEnabled && idle.Code == "", "view: idle has nothing to leave or copy");

        var needsMod = View(AppStatus.Idle, Mod(ModState.NotInstalled));
        Check.That(needsMod is { HostEnabled: false, JoinEnabled: false, Mod.Label: "Install", ModEnabled: true }, "view: no mod, only the mod button");
        Check.That(View(AppStatus.Idle, Mod(ModState.UpdateAvailable)) is { HostEnabled: true, Mod.Label: "Update" }, "view: an update still hosts");
        Check.That(View(AppStatus.Idle, Mod(ModState.Modified)) is { HostEnabled: true, Mod.Label: "Repair" }, "view: a modified mod still hosts");
        Check.That(!View(AppStatus.Idle, Mod(ModState.UpdateAvailable), locked: true).ModEnabled, "view: a staged update locks the mod button");
        Check.That(!View(AppStatus.Idle, Mod(ModState.UpdateAvailable) with { GameRunning = true }).ModEnabled, "view: the mod button waits for the game to close");

        var offline = View(new AppStatus(AppState.Offline));
        Check.That(offline is { InLobby: false, HostEnabled: false, JoinEnabled: false, CodeEnabled: false, LeaveEnabled: false }, "view: offline hosts and joins nothing");
        Check.That(View(new AppStatus(AppState.Offline), Mod(ModState.NotInstalled)).ModEnabled, "view: offline is free for the mod button");

        var connecting = View(new AppStatus(AppState.Connecting));
        Check.That(connecting is { Title: "Lobby", InLobby: true, Code: "", CopyEnabled: false, InviteEnabled: false, LeaveEnabled: true, HostEnabled: false, JoinEnabled: false },
            "view: connecting shows the lobby row, no code, Leave works");

        var hosting = View(new AppStatus(AppState.Hosting, LobbyId) { GameId = "re0" });
        Check.That(hosting is { Code: "109775243712345678", CopyEnabled: true, InviteEnabled: true, LeaveEnabled: true, ModEnabled: false }, "view: a lobby with its code");

        var rejoining = View(new AppStatus(AppState.Rejoining, LobbyId) { GameId = "re0", RejoinAttempt = 2 });
        Check.That(rejoining is { InLobby: true, CopyEnabled: true, InviteEnabled: false, LeaveEnabled: true }, "view: rejoining copies the code, cannot invite, can leave");
        var waiting = View(new AppStatus(AppState.WaitingForHost, LobbyId) { GameId = "re0", SecondsLeft = 161 });
        Check.That(waiting is { InLobby: true, CopyEnabled: true, InviteEnabled: false, LeaveEnabled: true }, "view: waiting for the host the same");

        Check.That(View(new AppStatus(AppState.GameRunning, LobbyId), relaunch: true).Relaunch, "view: relaunch shows in a lobby");
        Check.That(!View(AppStatus.Idle, relaunch: true).Relaunch, "view: relaunch needs a lobby");

        var friends = View(AppStatus.Idle with { FriendLobbies = [new FriendLobby(7, "Amy", 71, "sr2")] });
        Check.That(friends.Friends.Count == 1 && friends.Friends[0].Game == Sr2.Name, "view: friends hosting show while idle");
        Check.That(View(new AppStatus(AppState.Hosting, LobbyId) { FriendLobbies = [new FriendLobby(7, "Amy", 71, "sr2")] }).Friends.Count == 0, "view: not in a lobby");
    }
}
