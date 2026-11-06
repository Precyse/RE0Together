using CoopLauncher;

namespace LauncherTests;

/// <summary>NativeJoin.Readable: every state the SR2 mod writes reads as a sentence, a remedy where there is one, and a state
/// this launcher does not know still shows.</summary>
public static class NativeJoinTests
{
    private static bool Reads(string line, string expected) => NativeJoin.Readable(line) == expected;

    public static void Run()
    {
        Check.That(Reads("", ""), "no status line reads as nothing");
        Check.That(Reads("state=wrong_mode", "Wrong menu: host from Co-op > LAN, not Online"), "wrong_mode names the right menu");
        Check.That(Reads("state=login_needed", "Not signed in: sign in to an online profile in the game's Co-op > Online menu"), "login_needed says what to sign into");
        Check.That(Reads("state=advertising", "Hosting, waiting for players to join"), "advertising");
        Check.That(Reads("state=in_session detail=3", "In the session with 3 players"), "in_session counts the players");
        Check.That(Reads("state=in_session detail=1", "In the session with 1 player"), "in_session with one player is singular");
        Check.That(Reads("state=in_session", "In the session"), "in_session without a count");
        Check.That(Reads("state=searching detail=0", "Looking for the host's game"), "searching with nothing listed");
        Check.That(Reads("state=searching detail=3", "Looking for the host's game (3 listed)"), "searching shows the list size");
        Check.That(Reads("state=found", "Found the host's game, joining"), "found");
        Check.That(Reads("state=dialog", "Answer the game's dialog (pick a save or New Game) to continue"), "dialog tells the player to answer");
        Check.That(Reads("state=joining", "Joining the host's game"), "joining");
        Check.That(Reads("state=joined", "In the host's game"), "joined");
        Check.That(Reads("state=stopped", "Co-op stopped"), "stopped");
        Check.That(Reads("state=waiting detail=link", "Waiting for the connection to the host"), "waiting for the link");
        Check.That(Reads("state=waiting detail=front end", "Waiting for the game's main menu (close any open menu or dialog)"), "waiting for the front end");
        Check.That(Reads("state=waiting detail=login", "Signing in to the online service"), "waiting for the login");
        Check.That(NativeJoin.Readable("state=failed detail=version").Contains("version differs"), "failed version");
        Check.That(NativeJoin.Readable("state=failed detail=password").Contains("password"), "failed password");
        Check.That(NativeJoin.Readable("state=failed detail=host_address").Contains("not the host's"), "failed host_address");
        Check.That(NativeJoin.Readable("state=failed detail=no join").Contains("several tries"), "failed no join");
        Check.That(Reads("state=failed detail=odd", "Could not join odd"), "an unknown failure keeps its detail");
        Check.That(Reads("state=some_new_state detail=7", "some new state 7"), "an unknown state shows as the mod wrote it");
    }
}
