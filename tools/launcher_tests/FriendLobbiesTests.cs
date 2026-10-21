using CoopLauncher;

namespace LauncherTests;

/// <summary>FriendLobbies.Select: which friends' lobbies are offered, and in which order.</summary>
public static class FriendLobbiesTests
{
    private static readonly string[] Known = ["re0", "sr2"];

    private static FriendLobby Friend(string name, ulong lobby, string game = "sr2") => new(lobby + 1000, name, lobby, game);

    public static void Run()
    {
        var found = new[]
        {
            Friend("zed", 5), Friend("amy", 6, "re0"), Friend("Bob", 7), Friend("idle", 0), Friend("stranger", 8, "other"), Friend("amy twin", 6, "re0"),
        };
        var rows = FriendLobbies.Select(found, Known, FriendLobbies.MaxFriendRows);
        Check.That(rows.Select(r => r.Name).SequenceEqual(["amy", "Bob", "zed"]), "no lobby and unknown games are dropped, a lobby shows once, sorted by name ignoring case");

        var many = Enumerable.Range(1, 9).Select(i => Friend($"friend {i}", (ulong)i)).ToList();
        Check.That(FriendLobbies.Select(many, Known, FriendLobbies.MaxFriendRows).Count == FriendLobbies.MaxFriendRows, "at most the maximum rows");
        Check.That(FriendLobbies.Select(many, Known, 2).Select(r => r.Name).SequenceEqual(["friend 1", "friend 2"]), "the maximum applies after sorting");
        Check.That(FriendLobbies.Select([], Known, FriendLobbies.MaxFriendRows).Count == 0, "nothing found: no rows");
    }
}
