namespace CoopLauncher.Gui;

/// <summary>What the notice row's fix button does.</summary>
internal enum NoticeFix { None, InstallMod, UpdateMod, UpdateLauncher, Browse, JoinAgain, Switch }

/// <summary>What the Session section shows for one status: which row, which buttons are usable, the lobby code, the friends
/// hosting and the notice.</summary>
internal sealed record SessionView(
    string Title, bool InLobby, bool Native, bool HostEnabled, bool JoinEnabled, ModAction? Mod, bool ModEnabled, bool CodeEnabled,
    string Code, bool Relaunch, bool CopyEnabled, bool LeaveEnabled, IReadOnlyList<FriendCard> Friends, Notice? Notice);

/// <summary>The pure mapping from the app's status to what the window's cards, rows and notices say; unit tested
/// (<c>SessionCardsTests</c>).</summary>
internal static class SessionCards
{
    private const string HostRole = "Host";
    private const string GuestRole = "Guest";
    private const string YouSuffix = " / You";
    private const string OpenSeatName = "Open";
    private const string PingFormat = "{0} ms";
    private const string InviteWords = "Invite from {0}";
    private const string SwitchLabel = "Switch";
    private const double FullProgress = 100.0;

    /// <summary>One card per seat: the host first, then the other members, then open seats up to the seat count (never fewer
    /// than the members). The characters are in seat order.</summary>
    public static IReadOnlyList<SeatCard> Seats(AppStatus status, IReadOnlyList<string> characters, int seats)
    {
        var members = status.Players.OrderByDescending(player => player.IsHost).ToList();
        var cards = new List<SeatCard>();
        for (var seat = 0; seat < Math.Max(seats, members.Count); seat++)
        {
            var character = seat < characters.Count ? characters[seat] : string.Empty;
            cards.Add(seat < members.Count
                ? Taken(members[seat], character)
                : new SeatCard(0, OpenSeatName, string.Empty, character, string.Empty, Theme.Dim, string.Empty, null, Open: true));
        }
        return cards;
    }

    /// <summary>The friends hosting a lobby, in the order the app gave them, with their game's name.</summary>
    public static IReadOnlyList<FriendCard> Friends(AppStatus status, IEnumerable<GameProfile> games)
    {
        var known = games.ToList();
        return status.FriendLobbies
            .Select(friend => new FriendCard(friend.FriendId, friend.Name, known.FirstOrDefault(game => game.Id == friend.GameId)?.Name ?? friend.GameId, friend.LobbyId))
            .ToList();
    }

    /// <summary>The fix a refusal offers (the table of the Session section), or that of the invite received while in a lobby.
    /// A newer launcher build of 0 means none is known.</summary>
    public static NoticeFix FixFor(AppStatus status, int newerBuild)
    {
        if (status.Refusal is not { } refusal) return status.Invite != null ? NoticeFix.Switch : NoticeFix.None;
        return refusal.Kind switch
        {
            RefusalKind.ModNotInstalled => NoticeFix.InstallMod,
            RefusalKind.ModFiles => NoticeFix.UpdateMod,
            RefusalKind.LauncherBuild or RefusalKind.UnknownGame => newerBuild > 0 ? NoticeFix.UpdateLauncher : NoticeFix.None,
            RefusalKind.GameNotFound => NoticeFix.Browse,
            RefusalKind.LobbyFull or RefusalKind.LobbyClosed or RefusalKind.LobbyNotAllowed or RefusalKind.LobbyFailed
                or RefusalKind.NotCoopLobby or RefusalKind.SteamOffline or RefusalKind.SaveTimedOut or RefusalKind.RejoinGaveUp =>
                refusal.LobbyId != 0 ? NoticeFix.JoinAgain : NoticeFix.None,
            _ => NoticeFix.None,
        };
    }

    /// <summary>The one notice to show: a refusal (with Report) wins over an invite; none when there is neither.</summary>
    public static Notice? NoticeFor(AppStatus status, int newerBuild)
    {
        var fix = FixFor(status, newerBuild);
        var label = FixLabel(fix, newerBuild);
        if (status.Refusal is { } refusal)
            return new Notice($"{refusal.Kind}/{refusal.GameId}/{refusal.LobbyId}", StatusText.Refusal(refusal), label, Report: true);
        return status.Invite is { } invite ? new Notice($"invite/{invite.LobbyId}", string.Format(InviteWords, invite.FromName), label, Report: false) : null;
    }

    /// <summary>A refusal that the player's own fix has already resolved, so it can go: the game is found, its mod installed or
    /// its required file present.</summary>
    public static bool Resolved(Refusal refusal, GameStatus game) => refusal.Kind switch
    {
        RefusalKind.ModNotInstalled or RefusalKind.GameNotFound or RefusalKind.RequirementMissing => game.CanHost,
        _ => false,
    };

    /// <summary>The Session section for one status of the selected game; <paramref name="games"/> are all the games, to name the
    /// friends' lobbies.</summary>
    public static SessionView View(AppStatus status, GameProfile game, GameStatus mod, bool modLocked, bool relaunch, int newerBuild,
        IEnumerable<GameProfile> games)
    {
        var idle = status.State == AppState.Idle;
        var free = idle || status.State == AppState.Offline;
        var code = !free && status.LobbyId != 0 ? status.LobbyId.ToString() : string.Empty;
        var action = mod.Action;
        return new SessionView(
            Title: free ? "Session" : "Lobby", InLobby: !free, Native: game.NativeSession,
            HostEnabled: (game.NativeSession ? free : idle) && mod.CanHost, JoinEnabled: idle && mod.CanHost,
            Mod: action, ModEnabled: free && !modLocked && action is { Enabled: true }, CodeEnabled: idle,
            Code: code, Relaunch: !free && relaunch, CopyEnabled: code.Length > 0,
            LeaveEnabled: !free, Friends: idle ? Friends(status, games) : [],
            Notice: NoticeFor(status, newerBuild));
    }

    private static SeatCard Taken(PlayerSlot player, string character) => new(
        player.Id, player.Name, (player.IsHost ? HostRole : GuestRole) + (player.IsLocal ? YouSuffix : string.Empty), character,
        StatusText.Seat(player), StatusText.SeatLamp(player),
        !player.IsLocal && player.RttMs is { } rtt ? string.Format(PingFormat, rtt) : string.Empty,
        player.Step == SessionStep.ReceivingSave ? player.Percent / FullProgress : null, Open: false);

    private static string? FixLabel(NoticeFix fix, int newerBuild) => fix switch
    {
        NoticeFix.InstallMod => "Install",
        NoticeFix.UpdateMod => "Update",
        NoticeFix.UpdateLauncher => $"Update to {newerBuild}",
        NoticeFix.Browse => "Browse",
        NoticeFix.JoinAgain => "Join again",
        NoticeFix.Switch => SwitchLabel,
        _ => null,
    };
}
