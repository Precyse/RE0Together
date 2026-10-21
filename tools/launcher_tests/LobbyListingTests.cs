using CoopLauncher;

namespace LauncherTests;

/// <summary>LobbyListing.Decode from a lobby's data and RosterCodec (the roster text the host publishes).</summary>
public static class LobbyListingTests
{
    public static void Run()
    {
        Decoding();
        Roster();
        Equality();
    }

    private static Func<string, string> Data(Dictionary<string, string> values) => key => values.GetValueOrDefault(key, string.Empty);

    private static void Decoding()
    {
        var values = new Dictionary<string, string>
        {
            [LobbyKeys.Game] = "re0", [LobbyKeys.Host] = "Anna", [LobbyKeys.Version] = "412", [LobbyKeys.Mod] = "408", [LobbyKeys.GameBuild] = "9001",
            [LobbyKeys.HostOptions] = "partner_mode=state;character=rebecca", [LobbyKeys.Started] = "1760000000",
            [LobbyKeys.Roster] = RosterCodec.Encode([new RosterEntry("Anna", SessionStep.InGame), new RosterEntry("Bo", SessionStep.ReceivingSave)]),
        };
        var listing = LobbyListing.Decode(Data(values), 77, 12, 2, 4, 63);
        Check.That(listing.LobbyId == 77 && listing.HostId == 12 && listing.Players == 2 && listing.MaxPlayers == 4 && listing.PingMs == 63, "ids, counts and ping come from the arguments");
        Check.That(listing.GameId == "re0" && listing.HostName == "Anna", "game and host name");
        Check.That(listing.LauncherBuild == 412 && listing.ModBuild == 408 && listing.GameBuild == 9001, "the three builds");
        Check.That(listing.HostOptions["partner_mode"] == "state" && listing.HostOptions["character"] == "rebecca", "the host options");
        Check.That(listing.StartedUnix == 1760000000, "the start of play");
        Check.That(listing.Roster.SequenceEqual([new RosterEntry("Anna", SessionStep.InGame), new RosterEntry("Bo", SessionStep.ReceivingSave)]), "the roster");

        var bare = LobbyListing.Decode(Data(new Dictionary<string, string> { [LobbyKeys.Game] = "sr2" }), 5, 6, 1, 2, null);
        Check.That(bare.LauncherBuild == 0 && bare.ModBuild == 0 && bare.GameBuild == 0 && bare.StartedUnix == 0 && bare.PingMs == null, "missing numbers are 0 and the ping stays unknown");
        Check.That(bare.HostName == "" && bare.HostOptions.Count == 0 && bare.Roster.Count == 0, "missing text is empty");

        var broken = LobbyListing.Decode(
            Data(new Dictionary<string, string> { [LobbyKeys.Version] = "x12", [LobbyKeys.Mod] = "-3", [LobbyKeys.GameBuild] = "99999999999", [LobbyKeys.Started] = "soon" }), 1, 2, 1, 2, null);
        Check.That(broken.LauncherBuild == 0 && broken.ModBuild == 0 && broken.GameBuild == 0 && broken.StartedUnix == 0, "numbers that are not numbers are 0");
    }

    private static void Roster()
    {
        var roster = new[] { new RosterEntry("Anna", SessionStep.Lobby), new RosterEntry("Bo Peep", SessionStep.Refused), new RosterEntry("", SessionStep.Ready) };
        Check.That(RosterCodec.Decode(RosterCodec.Encode(roster)).SequenceEqual(roster), "a roster survives a round trip");
        foreach (var step in Enum.GetValues<SessionStep>())
            Check.That(RosterCodec.Decode(RosterCodec.Encode([new RosterEntry("n", step)])).SequenceEqual([new RosterEntry("n", step)]), $"{step} survives a round trip");
        Check.That(RosterCodec.Encode([new RosterEntry("Anna", SessionStep.InGame), new RosterEntry("Bo", SessionStep.Lobby)]) == "Anna\tInGame\nBo\tLobby", "name TAB step, lines joined by LF");
        Check.That(RosterCodec.Encode([]) == "", "an empty roster is empty text");

        var clean = RosterCodec.Decode(RosterCodec.Encode([new RosterEntry("A\tB\nC\r\nD", SessionStep.InGame), new RosterEntry("Next", SessionStep.Lobby)]));
        Check.That(clean.Count == 2 && clean[0] == new RosterEntry("A B C  D", SessionStep.InGame) && clean[1].Name == "Next", "tabs and line breaks in a name become spaces");

        Check.That(RosterCodec.Decode("").Count == 0, "empty text decodes to nothing");
        Check.That(RosterCodec.Decode("Anna\tInGame\r\nBo\tLobby\r\n").Count == 2, "CRLF lines decode");
        var skipped = RosterCodec.Decode("no tab here\nAnna\tDancing\nBo\t5\nCy\tLobby,InGame\nDi\t\nEd\tInGame\tExtra\n\nFay\tGameClosed");
        Check.That(skipped.SequenceEqual([new RosterEntry("Fay", SessionStep.GameClosed)]), "a line without a tab, an unknown or numeric step, or extra fields is skipped");
    }

    private static void Equality()
    {
        var values = new Dictionary<string, string> { [LobbyKeys.Game] = "re0", [LobbyKeys.HostOptions] = "a=1", [LobbyKeys.Roster] = "x\tLobby" };
        var one = LobbyListing.Decode(Data(values), 1, 2, 1, 2, 10);
        var same = LobbyListing.Decode(Data(values), 1, 2, 1, 2, 10);
        Check.That(one.Equals(same) && one.GetHashCode() == same.GetHashCode(), "two decodes of the same data are equal (options and roster by content)");
        Check.That(!one.Equals(LobbyListing.Decode(Data(values), 1, 2, 2, 2, 10)), "another player count differs");
        Check.That(!one.Equals(LobbyListing.Decode(Data(values), 1, 2, 1, 2, 11)), "another ping differs");
        values[LobbyKeys.Roster] = "x\tInGame";
        Check.That(!one.Equals(LobbyListing.Decode(Data(values), 1, 2, 1, 2, 10)), "another roster differs");
        values[LobbyKeys.Roster] = "x\tLobby";
        values[LobbyKeys.HostOptions] = "a=2";
        Check.That(!one.Equals(LobbyListing.Decode(Data(values), 1, 2, 1, 2, 10)), "other host options differ");
        Check.That(!one.Equals(null), "never equal to null");
    }
}
