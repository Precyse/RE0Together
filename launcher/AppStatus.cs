namespace CoopLauncher;

/// <summary>Rejoining: the lobby connection was lost and is being rebuilt; WaitingForHost: a guest's host left and its new
/// lobby is awaited; Offline: the main loop could not start (see <see cref="OfflineReason"/>) and is retrying.</summary>
public enum AppState { Idle, Connecting, Hosting, Joined, GameRunning, PeerConnected, Rejoining, WaitingForHost, Offline }

/// <summary>Why the app is offline: Steam is not running (or stopped), or something else stopped the loop.</summary>
public enum OfflineReason { None, SteamNotRunning }

/// <summary>One lobby member as the window shows it. Status is what its game reports (rendezvous games), else empty; Step
/// and Percent are what its launcher published (<see cref="SessionStep"/>); RttMs, Held and Silent come from the session.</summary>
public sealed record PlayerSlot(
    ulong Id, string Name, bool IsHost, bool IsLocal, string Status = "",
    SessionStep Step = SessionStep.None, int Percent = 0, int? RttMs = null, bool Held = false, bool Silent = false);

/// <summary>A friend hosting a lobby of a game this launcher knows.</summary>
public sealed record FriendLobby(ulong FriendId, string Name, ulong LobbyId, string GameId);

/// <summary>An invite to another lobby, received while already in one.</summary>
public sealed record PendingInvite(ulong LobbyId, ulong FromId, string FromName);

/// <summary>What the main loop is doing right now, for display.</summary>
public sealed record AppStatus(AppState State, ulong LobbyId = 0, int? RttMs = null)
{
    public static readonly AppStatus Idle = new(AppState.Idle);

    /// <summary>Lobby members, host first.</summary>
    public IReadOnlyList<PlayerSlot> Players { get; init; } = Array.Empty<PlayerSlot>();

    /// <summary>The game of the open lobby, null when there is none.</summary>
    public string? GameId { get; init; }

    /// <summary>The game's characters in seat order (host first) for the open lobby; empty when there is none.</summary>
    public IReadOnlyList<string> SeatCharacters { get; init; } = Array.Empty<string>();

    /// <summary>Why the last start was refused; shown until the next Host/Join or <see cref="App.ClearRefusal"/>.</summary>
    public Refusal? Refusal { get; init; }

    /// <summary>The session has the adapter link (a rendezvous game with the bridge expects its adapter to connect).</summary>
    public bool Bridged { get; init; }

    /// <summary>Where this launcher is in getting the session going.</summary>
    public SessionStep Step { get; init; }

    /// <summary>How much of the host's save has arrived (guest), 0 to 100.</summary>
    public int SavePercent { get; init; }

    /// <summary>How many players the open lobby takes (the host's choice where the game offers one).</summary>
    public int Seats { get; init; }

    /// <summary>The host's option values the open lobby carries.</summary>
    public IReadOnlyDictionary<string, string> HostOptions { get; init; } = new Dictionary<string, string>();

    /// <summary>Rejoining: the attempts made so far.</summary>
    public int RejoinAttempt { get; init; }

    /// <summary>Rejoining or waiting for the host: whole seconds before giving up.</summary>
    public int SecondsLeft { get; init; }

    public OfflineReason Offline { get; init; }

    /// <summary>When the launcher started the game (<c>Environment.TickCount64</c>); null when it did not.</summary>
    public long? LaunchedAtMs { get; init; }

    /// <summary>Friends hosting a lobby, offered while idle.</summary>
    public IReadOnlyList<FriendLobby> FriendLobbies { get; init; } = Array.Empty<FriendLobby>();

    /// <summary>An invite received while in a lobby, waiting for the player to switch to it.</summary>
    public PendingInvite? Invite { get; init; }

    public bool Equals(AppStatus? other) =>
        other is not null && State == other.State && LobbyId == other.LobbyId && RttMs == other.RttMs && GameId == other.GameId
        && Refusal == other.Refusal && Bridged == other.Bridged && Step == other.Step && SavePercent == other.SavePercent
        && Seats == other.Seats && RejoinAttempt == other.RejoinAttempt && SecondsLeft == other.SecondsLeft
        && Offline == other.Offline && LaunchedAtMs == other.LaunchedAtMs && Invite == other.Invite
        && Players.SequenceEqual(other.Players) && SeatCharacters.SequenceEqual(other.SeatCharacters)
        && FriendLobbies.SequenceEqual(other.FriendLobbies) && SameValues(HostOptions, other.HostOptions);

    public override int GetHashCode() => HashCode.Combine(State, LobbyId, RttMs, Players.Count);

    private static bool SameValues(IReadOnlyDictionary<string, string> a, IReadOnlyDictionary<string, string> b) =>
        a.Count == b.Count && a.All(pair => b.TryGetValue(pair.Key, out var value) && value == pair.Value);
}
