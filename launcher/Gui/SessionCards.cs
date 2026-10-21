namespace CoopLauncher.Gui;

/// <summary>What the notice row's fix button does.</summary>
internal enum NoticeFix { None, InstallMod, UpdateMod, UpdateLauncher, Browse, JoinAgain, Switch }

/// <summary>What the home hero and friends show for one status: which buttons are usable, the mod button, the friends hosting
/// and the notice.</summary>
internal sealed record HomeView(
    bool Native, bool HostEnabled, bool JoinEnabled, ModAction? Mod, bool ModEnabled, bool CodeEnabled,
    IReadOnlyList<FriendCard> Friends, Notice? Notice);

/// <summary>The pure mapping from the app's status to what the home screen's buttons, friends and notices say; unit tested
/// (<c>SessionCardsTests</c>).</summary>
internal static class SessionCards
{
    private const string InviteWords = "Invite from {0}";
    private const string SwitchLabel = "Switch";

    /// <summary>The friends hosting a lobby, in the order the app gave them, with their game's name, their lobby's player count
    /// when it is known, and whether the game is the selected one.</summary>
    public static IReadOnlyList<FriendCard> Friends(AppStatus status, IEnumerable<GameProfile> games, string? selectedGameId)
    {
        var known = games.ToList();
        return status.FriendLobbies
            .Select(friend => new FriendCard(
                friend.FriendId, friend.Name, known.FirstOrDefault(game => game.Id == friend.GameId)?.Name ?? friend.GameId, friend.LobbyId,
                friend.GameId == selectedGameId, friend.Listing is { } listing ? $"{listing.Players}/{listing.MaxPlayers}" : string.Empty))
            .ToList();
    }

    /// <summary>The fix a refusal offers (the table of the notice row), or that of the invite received while in a lobby.
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

    /// <summary>The home screen for one status of the selected game; <paramref name="games"/> are all the games, to name the
    /// friends' lobbies.</summary>
    public static HomeView Home(AppStatus status, GameProfile game, GameStatus mod, bool modLocked, int newerBuild, IEnumerable<GameProfile> games)
    {
        var idle = status.State == AppState.Idle;
        var free = idle || status.State == AppState.Offline;
        var action = mod.Action;
        return new HomeView(
            Native: game.NativeSession, HostEnabled: (game.NativeSession ? free : idle) && mod.CanHost, JoinEnabled: idle && mod.CanHost,
            Mod: action, ModEnabled: free && !modLocked && action is { Enabled: true }, CodeEnabled: idle,
            Friends: idle ? Friends(status, games, game.Id) : [], Notice: NoticeFor(status, newerBuild));
    }

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
