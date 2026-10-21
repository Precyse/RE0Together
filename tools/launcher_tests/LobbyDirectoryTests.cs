using CoopLauncher;

namespace LauncherTests;

/// <summary>LobbyDirectory.Select: which found lobbies are offered, and in which order.</summary>
public static class LobbyDirectoryTests
{
    private static readonly string[] Known = ["re0", "sr2"];
    private const ulong Me = 99;

    private static LobbyListing Lobby(ulong id, int? ping, string game = "re0", ulong host = 1) => new(
        id, game, host, "host", 1, 2, 412, 408, 9001, new Dictionary<string, string>(), [], 0, ping);

    public static void Run()
    {
        var found = new[]
        {
            Lobby(1, 80), Lobby(2, null), Lobby(3, 20, "sr2"), Lobby(4, 5, "other"), Lobby(5, 10, host: Me), Lobby(1, 5), Lobby(6, 80), Lobby(7, 0),
        };
        var rows = LobbyDirectory.Select(found, Known, Me);
        Check.That(rows.Select(r => r.LobbyId).SequenceEqual(new ulong[] { 7, 3, 1, 6, 2 }), "known games only, not my own, one per lobby (the first), ping ascending with ties in found order and unknown last");
        Check.That(rows.Single(r => r.LobbyId == 1).PingMs == 80, "the first of a repeated lobby is the one kept");
        Check.That(LobbyDirectory.Select([], Known, Me).Count == 0, "nothing found: no rows");
        Check.That(LobbyDirectory.Select([Lobby(1, 5, "other")], Known, Me).Count == 0, "an unknown game alone: no rows");
        Check.That(LobbyDirectory.Select([Lobby(1, null), Lobby(2, null)], Known, Me).Select(r => r.LobbyId).SequenceEqual(new ulong[] { 1, 2 }), "all unknown pings keep the found order");
        Check.That(LobbyDirectory.Select([Lobby(1, 5, host: Me)], Known, 0).Count == 1, "with no own id nothing is excluded");
    }
}
