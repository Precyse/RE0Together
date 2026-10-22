namespace CoopLauncher.Gui;

/// <summary>A friend who is hosting a lobby: the card shows their avatar, name, the game's name and the lobby's player count
/// (empty while unknown). Selected says the friend's game is the one picked in the rail: its button is the primary JOIN.</summary>
public sealed record FriendCard(ulong AvatarId, string Name, string Game, ulong LobbyId, bool Selected = false, string Count = "");
