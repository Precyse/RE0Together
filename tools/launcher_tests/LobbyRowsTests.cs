using CoopLauncher;
using CoopLauncher.Gui;

namespace LauncherTests;

/// <summary>The rows of the Lobbies tab: which lobbies show and what each row's button does.</summary>
public static class LobbyRowsTests
{
    private const int Local = 412;
    private const int Newer = 413;

    private static readonly GameProfile Re0 = GameProfile.Load("re0");
    private static readonly GameProfile Sr2 = GameProfile.Load("sr2");
    private static readonly IReadOnlyList<GameProfile> Games = [Re0, Sr2];

    private static GameStatus Status(int installedMod = 412, int packageBuild = 412, int gameBuild = 1076239, bool found = true) =>
        new(new ModStatus(ModState.Installed, installedMod), found, packageBuild, gameBuild);

    private static LobbyListing Listing(ulong id = 1, string game = "sr2", int players = 1, int max = 4, int launcher = Local, int mod = 412, int build = 1076239,
        int? ping = 42) =>
        new(id, game, 7, "Zed", players, max, launcher, mod, build, new Dictionary<string, string>(), [], 0, ping);

    private static IReadOnlyList<LobbyRowModel> Rows(AppStatus status, LobbyFilter filter = LobbyFilter.All, string? game = null) =>
        LobbyRowModels.Of(status, filter, game, Games, _ => Status(), Local, 0);

    private static LobbyAction Action(LobbyListing listing, GameStatus? status = null, int newer = 0) =>
        LobbyRowModels.ActionFor(listing, status ?? Status(), Local, newer);

    public static void Run()
    {
        Selection();
        Actions();
        Columns();
    }

    private static void Selection()
    {
        var status = AppStatus.Idle with
        {
            Lobbies = [Listing(1), Listing(2, "re0", max: 2), Listing(3, "unknown-game")],
            FriendLobbies = [new FriendLobby(9, "Amy", 4, "re0", Listing(4, "re0", max: 2)), new FriendLobby(8, "Kai", 2, "re0", Listing(2, "re0", max: 2)), new FriendLobby(6, "Bo", 5, "sr2")],
        };
        var all = Rows(status);
        Check.That(all.Select(row => row.Listing.LobbyId).SequenceEqual([1UL, 2UL, 4UL]), "rows: the search, then friends' lobbies it missed, an unknown game dropped, one row per lobby");
        Check.That(Rows(status, LobbyFilter.Friends).Select(row => row.Listing.LobbyId).SequenceEqual([4UL, 2UL]), "rows: the Friends filter lists the friends' lobbies that carry a listing");
        Check.That(Rows(status, game: "re0").Select(row => row.Listing.LobbyId).SequenceEqual([2UL, 4UL]), "rows: the game filter narrows to one game");
        Check.That(Rows(status, game: "ds2").Count == 0, "rows: a game without lobbies is empty");
        Check.That(Rows(AppStatus.Idle).Count == 0, "rows: nothing found, no rows");
    }

    private static void Actions()
    {
        Check.That(Action(Listing()) == LobbyAction.Join, "action: a matching lobby joins");
        Check.That(Action(Listing(players: 4)) == LobbyAction.Full, "action: a full lobby is Full");
        Check.That(Action(Listing(launcher: 410)) == LobbyAction.Unavailable, "action: another launcher build is unavailable");
        Check.That(Action(Listing(launcher: Newer), newer: Newer) == LobbyAction.UpdateLauncher, "action: the newer launcher build on offer is an Update");
        Check.That(Action(Listing(launcher: 410, players: 4)) == LobbyAction.Unavailable, "action: the launcher build is checked before the lobby is full");
        Check.That(Action(Listing(mod: 409)) == LobbyAction.Unavailable, "action: another mod build that nothing here provides is unavailable");
        Check.That(Action(Listing(mod: 409), Status(installedMod: 409, packageBuild: 412)) == LobbyAction.Join, "action: the installed mod matching the host's joins");
        Check.That(Action(Listing(mod: 412), Status(installedMod: 409, packageBuild: 412)) == LobbyAction.UpdateMod, "action: the package carrying the host's mod build installs it");
        Check.That(Action(Listing(mod: Newer), Status(installedMod: 412), Newer) == LobbyAction.UpdateLauncher, "action: a mod build only the newer launcher has is a launcher Update");
        Check.That(Action(Listing(build: 999)) == LobbyAction.Unavailable, "action: another game build is unavailable");
        Check.That(Action(Listing(), Status(found: false)) == LobbyAction.Unavailable, "action: a game that is not installed here is unavailable");
        Check.That(Action(Listing(launcher: 0, mod: 0, build: 0)) == LobbyAction.Join, "action: builds a host does not publish never differ");
        Check.That(Action(Listing(players: 4, mod: 409)) == LobbyAction.Full, "action: full comes before the mod check");

        Check.That(LobbyRowModels.ModDiffers(Listing(mod: 409), Status()), "mod: the host's build differs from the installed one");
        Check.That(!LobbyRowModels.ModDiffers(Listing(mod: 412), Status()) && !LobbyRowModels.ModDiffers(Listing(mod: 0), Status()), "mod: the same or an unknown build does not differ");
    }

    private static void Columns()
    {
        Check.That(LobbyRowModels.Ping(Listing(ping: 63)) == "63 ms" && LobbyRowModels.Ping(Listing(ping: null)) == "", "columns: the ping reads N ms, empty while unknown");
        Check.That(LobbyRowModels.Players(Listing(players: 3, max: 4)) == "3/4", "columns: players read n/max");
    }
}
