using CoopLauncher;
using CoopLauncher.Gui;

namespace LauncherTests;

/// <summary>The friends, the notice and the home view worked out from an AppStatus.</summary>
public static class SessionCardsTests
{
    private const ulong LobbyId = 109775243712345678;
    private const int NewerBuild = 413;

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
        Friends();
        Fixes();
        Notices();
        Resolution();
        Homes();
    }

    private static void Friends()
    {
        var listing = new LobbyListing(71, "sr2", 7, "Zed", 3, 4, 412, 412, 1076239, new Dictionary<string, string>(), [], 0, 63);
        var status = AppStatus.Idle with
        {
            FriendLobbies = [new FriendLobby(7, "Zed", 71, "sr2", listing), new FriendLobby(8, "Amy", 81, "re0")],
        };
        var cards = SessionCards.Friends(status, [Re0, Sr2], "re0");
        Check.That(cards.Count == 2 && cards[0] == new FriendCard(7, "Zed", Sr2.Name, 71, Selected: false, Count: "3/4") && cards[1] == new FriendCard(8, "Amy", Re0.Name, 81, Selected: true, Count: ""),
            "friends: the app's order, with the game's name, the player count when the listing is known, and whether the game is the selected one");
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
                     RefusalKind.NotCoopLobby, RefusalKind.SteamOffline, RefusalKind.SaveTimedOut, RefusalKind.SaveDiffers, RefusalKind.RejoinGaveUp })
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

        var steamOff = new AppStatus(AppState.Offline) { Offline = OfflineReason.SteamNotRunning };
        Check.That(SessionCards.NoticeFor(steamOff, 0) is { Words: "Steam not running", Action: "Start Steam", Report: false }, "notice: Steam not running offers Start Steam");
        Check.That(SessionCards.FixFor(steamOff, 0) == NoticeFix.StartSteam, "fix: Steam not running starts Steam");
        Check.That(SessionCards.NoticeFor(new AppStatus(AppState.Offline), 0) == null, "notice: none while offline for another reason");
    }

    private static void Resolution()
    {
        Check.That(SessionCards.Resolved(new Refusal(RefusalKind.ModNotInstalled), Mod(ModState.Installed)), "resolved: the mod was installed");
        Check.That(!SessionCards.Resolved(new Refusal(RefusalKind.ModNotInstalled), Mod(ModState.NotInstalled)), "resolved: not while the mod is missing");
        Check.That(SessionCards.Resolved(new Refusal(RefusalKind.GameNotFound), Mod(ModState.Installed)), "resolved: the game was found");
        Check.That(!SessionCards.Resolved(new Refusal(RefusalKind.ModFiles), Mod(ModState.Installed)), "resolved: differing files are not fixed by an installed mod");
        Check.That(!SessionCards.Resolved(new Refusal(RefusalKind.LobbyFull), Mod(ModState.Installed)), "resolved: a lobby refusal stays");
    }

    private static HomeView Home(AppStatus status, GameStatus? mod = null, bool locked = false) =>
        SessionCards.Home(status, Re0, mod ?? Mod(ModState.Installed), locked, 0, [Re0, Sr2]);

    private static void Homes()
    {
        var idle = Home(AppStatus.Idle);
        Check.That(idle is { HostEnabled: true, JoinEnabled: true, CodeEnabled: true, Mod: null, Native: false }, "home: idle with the mod installed");

        var needsMod = Home(AppStatus.Idle, Mod(ModState.NotInstalled));
        Check.That(needsMod is { HostEnabled: false, JoinEnabled: false, Mod.Label: "Install", ModEnabled: true }, "home: no mod, only the mod button");
        Check.That(Home(AppStatus.Idle, Mod(ModState.UpdateAvailable)) is { HostEnabled: true, Mod.Label: "Update" }, "home: an update still hosts");
        Check.That(Home(AppStatus.Idle, Mod(ModState.Modified)) is { HostEnabled: true, Mod.Label: "Repair" }, "home: a modified mod still hosts");
        Check.That(!Home(AppStatus.Idle, Mod(ModState.UpdateAvailable), locked: true).ModEnabled, "home: a staged update locks the mod button");
        Check.That(!Home(AppStatus.Idle, Mod(ModState.UpdateAvailable) with { GameRunning = true }).ModEnabled, "home: the mod button waits for the game to close");

        var offline = Home(new AppStatus(AppState.Offline));
        Check.That(offline is { HostEnabled: false, JoinEnabled: false, CodeEnabled: false }, "home: offline hosts and joins nothing");
        Check.That(Home(new AppStatus(AppState.Offline), Mod(ModState.NotInstalled)).ModEnabled, "home: offline is free for the mod button");

        var friends = Home(AppStatus.Idle with { FriendLobbies = [new FriendLobby(7, "Amy", 71, "sr2")] });
        Check.That(friends.Friends.Count == 1 && friends.Friends[0].Game == Sr2.Name && !friends.Friends[0].Selected, "home: friends hosting show while idle");
        Check.That(Home(new AppStatus(AppState.Hosting, LobbyId) { FriendLobbies = [new FriendLobby(7, "Amy", 71, "sr2")] }).Friends.Count == 0, "home: not in a lobby");
        Check.That(Home(AppStatus.Idle with { Refusal = new Refusal(RefusalKind.HostLeft) }).Notice is { Words: "Host left" }, "home: the notice comes with the view");
    }
}
