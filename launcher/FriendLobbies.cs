using Steamworks;

namespace CoopLauncher;

/// <summary>Friends who are hosting a lobby this launcher can join: read from Steam's friend list and rich presence (the
/// lobby and game the host's launcher publishes, see <see cref="LobbyPresence"/>). Loop thread only.</summary>
public static class FriendLobbies
{
    public const int MaxFriendRows = 4;

    /// <summary>Every immediate friend who is running this app (the launcher), with the lobby and game it publishes.</summary>
    public static List<FriendLobby> Read()
    {
        var found = new List<FriendLobby>();
        var app = SteamUtils.GetAppID();
        var count = SteamFriends.GetFriendCount(EFriendFlags.k_EFriendFlagImmediate);
        for (var i = 0; i < count; i++)
        {
            var friend = SteamFriends.GetFriendByIndex(i, EFriendFlags.k_EFriendFlagImmediate);
            if (!SteamFriends.GetFriendGamePlayed(friend, out var playing) || playing.m_gameID.AppID() != app) continue;
            var (lobby, game) = LobbyPresence.Read(friend.m_SteamID);
            found.Add(new FriendLobby(friend.m_SteamID, SteamFriends.GetFriendPersonaName(friend) ?? string.Empty, lobby, game));
        }
        return found;
    }

    /// <summary>The rows to offer: a lobby that exists, of a game this launcher has a profile for, once per lobby, sorted by
    /// the friend's name, at most <paramref name="max"/>. Pure.</summary>
    public static IReadOnlyList<FriendLobby> Select(IEnumerable<FriendLobby> found, IReadOnlyCollection<string> knownGames, int max) =>
        found.Where(friend => friend.LobbyId != 0 && knownGames.Contains(friend.GameId))
            .GroupBy(friend => friend.LobbyId).Select(group => group.First())
            .OrderBy(friend => friend.Name, StringComparer.OrdinalIgnoreCase)
            .Take(max).ToList();
}
