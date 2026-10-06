using Steamworks;

namespace CoopLauncher;

/// <summary>The host's open lobby as Steam rich presence (visible to friends running the launcher): a guest whose host
/// crashed and relaunched finds the new lobby here.</summary>
public static class LobbyPresence
{
    private const string KeyLobby = "cf_lobby";
    private const string KeyGame = "cf_game";

    public static void Publish(ulong lobbyId, string gameId)
    {
        SteamFriends.SetRichPresence(KeyLobby, lobbyId.ToString());
        SteamFriends.SetRichPresence(KeyGame, gameId);
    }

    public static void Clear() => SteamFriends.ClearRichPresence();

    /// <summary>The lobby and game the friend publishes; a lobby id of 0 when it publishes none.</summary>
    public static (ulong LobbyId, string GameId) Read(ulong friendId)
    {
        var friend = new CSteamID(friendId);
        SteamFriends.RequestFriendRichPresence(friend);
        var lobby = ulong.TryParse(SteamFriends.GetFriendRichPresence(friend, KeyLobby), out var id) ? id : 0;
        return (lobby, SteamFriends.GetFriendRichPresence(friend, KeyGame));
    }
}
