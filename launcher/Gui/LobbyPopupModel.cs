using System.Globalization;

namespace CoopLauncher.Gui;

/// <summary>How one seat of the lobby popup's roster reads: a member the roster names, a member it does not name (a plain
/// row) or an open seat.</summary>
internal enum PopupSeatKind { Named, Unnamed, Open }

/// <summary>One seat of the popup's roster; Entry is set for a named member and Host marks the host's row.</summary>
internal sealed record PopupSeat(PopupSeatKind Kind, RosterEntry? Entry, bool Host);

/// <summary>The pure rules of the lobby popup's contents (the roster's seats, the caption, the Mod, Game, Network and Playing
/// rows), used by the WinForms popup and by the web window's snapshot.</summary>
internal static class LobbyPopupModel
{
    private const string PlayersCaption = "PLAYERS {0}/{1}";
    private const string DefaultNetwork = "Steam";
    private const string TransportKey = "transport";
    private const int SecondsPerHour = 3600;

    /// <summary>The seats the popup lists: the host's capacity, never fewer than the roster, at least one.</summary>
    public static int SeatCount(LobbyListing listing) => Math.Max(Math.Max(listing.MaxPlayers, listing.Roster.Count), 1);

    /// <summary>PLAYERS n/max.</summary>
    public static string Caption(LobbyListing listing) => string.Format(CultureInfo.InvariantCulture, PlayersCaption, listing.Players, listing.MaxPlayers);

    /// <summary>The seats in order: the roster's entries (the one named like the host is the host's row, the first when none is),
    /// then members the roster does not name, then open seats.</summary>
    public static IReadOnlyList<PopupSeat> Seats(LobbyListing listing)
    {
        var hostSeat = Math.Max(0, listing.Roster.ToList().FindIndex(entry => entry.Name == listing.HostName));
        var seats = new List<PopupSeat>();
        for (var seat = 0; seat < SeatCount(listing); seat++)
        {
            if (seat < listing.Roster.Count) seats.Add(new PopupSeat(PopupSeatKind.Named, listing.Roster[seat], seat == hostSeat));
            else seats.Add(new PopupSeat(seat < listing.Players ? PopupSeatKind.Unnamed : PopupSeatKind.Open, null, false));
        }
        return seats;
    }

    /// <summary>The host's game build is the one installed here (a build of 0 is unknown and never differs).</summary>
    public static bool GameOk(LobbyListing listing, GameStatus mod) => listing.GameBuild == 0 || listing.GameBuild == mod.GameBuild;

    /// <summary>The label of the host's transport option when the game has one, else Steam.</summary>
    public static string Network(LobbyRowModel model)
    {
        var option = model.Game.Options?.FirstOrDefault(candidate => candidate.Key == TransportKey);
        var value = option?.Values.FirstOrDefault(v => model.Listing.HostOptions.TryGetValue(TransportKey, out var host) && v.Value == host);
        return value?.Label ?? DefaultNetwork;
    }

    /// <summary>The timer as <c>m:ss</c>, <c>h:mm:ss</c> past an hour, or empty when the game has not started.</summary>
    public static string PlayingText(long startedUnix, long unixNow)
    {
        if (startedUnix <= 0) return string.Empty;
        var seconds = Math.Max(0, unixNow - startedUnix);
        return seconds >= SecondsPerHour ? SessionScreens.HoursMinutesSeconds(seconds) : SessionScreens.MinutesSeconds(seconds);
    }
}
