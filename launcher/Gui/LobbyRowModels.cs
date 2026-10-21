namespace CoopLauncher.Gui;

/// <summary>Which lobbies the Lobbies tab lists: everyone's, or the friends'. (The Home server filter is drawn disabled and has no
/// code behind it.)</summary>
internal enum LobbyFilter { All, Friends }

/// <summary>What the button of a lobby row does: Join (the primary JOIN), Full (disabled), Update the launcher or the mod (both
/// read Update), or Unavailable (a disabled Join: the lobby cannot be entered from here).</summary>
internal enum LobbyAction { Join, Full, UpdateLauncher, UpdateMod, Unavailable }

/// <summary>One row of the Lobbies tab, and the lobby popup's own look: the lobby, its game, what its button does and whether the
/// host's mod build differs from the installed one (drawn red).</summary>
internal sealed record LobbyRowModel(LobbyListing Listing, GameProfile Game, LobbyAction Action, bool ModDiffers);

/// <summary>The pure mapping from the lobby search to the rows of the Lobbies tab; unit tested (<c>LobbyRowsTests</c>).</summary>
internal static class LobbyRowModels
{
    private const string PingFormat = "{0} ms";

    /// <summary>The rows for a status: the lobbies of the search plus the friends' lobbies it did not find (a friends-only lobby
    /// is not in a public search), narrowed to the filter and to one game (null: every game). A lobby of a game this launcher does
    /// not know is dropped. <paramref name="statusOf"/> is a game's mod state; <paramref name="localBuild"/> is this launcher's
    /// build and <paramref name="newerBuild"/> the newer one on offer (0 when none).</summary>
    public static IReadOnlyList<LobbyRowModel> Of(AppStatus status, LobbyFilter filter, string? gameId, IReadOnlyList<GameProfile> games,
        Func<string, GameStatus> statusOf, int localBuild, int newerBuild)
    {
        var friends = status.FriendLobbies.Select(friend => friend.Listing).OfType<LobbyListing>().ToList();
        var listings = filter == LobbyFilter.Friends
            ? friends
            : status.Lobbies.Concat(friends.Where(friend => status.Lobbies.All(lobby => lobby.LobbyId != friend.LobbyId))).ToList();
        var rows = new List<LobbyRowModel>();
        foreach (var listing in listings)
        {
            var game = games.FirstOrDefault(known => known.Id == listing.GameId);
            if (game == null || gameId != null && game.Id != gameId) continue;
            var mod = statusOf(game.Id);
            rows.Add(new LobbyRowModel(listing, game, ActionFor(listing, mod, localBuild, newerBuild), ModDiffers(listing, mod)));
        }
        return rows;
    }

    /// <summary>The host's mod build is known and is not the one installed here.</summary>
    public static bool ModDiffers(LobbyListing listing, GameStatus mod) => listing.ModBuild > 0 && listing.ModBuild != mod.Mod.Build;

    /// <summary>The button of a lobby, first that applies: another launcher build is an Update when it is the newer build on offer,
    /// else unavailable; a full lobby; another mod build is an Update when this launcher's package carries that build (it is
    /// installed) or when it is the newer launcher build on offer, else unavailable; a game or a build here that does not match the
    /// host's is unavailable; otherwise Join. A build of 0 is unknown and never differs.</summary>
    public static LobbyAction ActionFor(LobbyListing listing, GameStatus mod, int localBuild, int newerBuild)
    {
        if (listing.LauncherBuild != 0 && listing.LauncherBuild != localBuild)
            return newerBuild == listing.LauncherBuild ? LobbyAction.UpdateLauncher : LobbyAction.Unavailable;
        if (listing.MaxPlayers > 0 && listing.Players >= listing.MaxPlayers) return LobbyAction.Full;
        if (ModDiffers(listing, mod))
        {
            if (mod.PackageBuild == listing.ModBuild && mod.GameFound) return LobbyAction.UpdateMod;
            return newerBuild == listing.ModBuild ? LobbyAction.UpdateLauncher : LobbyAction.Unavailable;
        }
        if (!mod.GameFound || listing.GameBuild != 0 && listing.GameBuild != mod.GameBuild) return LobbyAction.Unavailable;
        return LobbyAction.Join;
    }

    /// <summary>The ping column: <c>N ms</c>, empty while unknown.</summary>
    public static string Ping(LobbyListing listing) => listing.PingMs is { } ping ? string.Format(PingFormat, ping) : string.Empty;

    /// <summary>The players column: <c>n/max</c>.</summary>
    public static string Players(LobbyListing listing) => $"{listing.Players}/{listing.MaxPlayers}";
}
