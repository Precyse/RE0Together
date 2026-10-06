namespace CoopLauncher;

/// <summary>Offline: the main loop could not start (Steam not running) and is retrying.</summary>
public enum AppState { Idle, Connecting, Hosting, Joined, GameRunning, PeerConnected, Offline }

/// <summary>One lobby member as the window shows it.</summary>
public sealed record PlayerSlot(string Name, bool IsHost, bool IsLocal);

/// <summary>What the main loop is doing right now, for display.</summary>
public sealed record AppStatus(AppState State, ulong LobbyId = 0, int? RttMs = null)
{
    public static readonly AppStatus Idle = new(AppState.Idle);

    /// <summary>Lobby members, host first.</summary>
    public IReadOnlyList<PlayerSlot> Players { get; init; } = Array.Empty<PlayerSlot>();

    /// <summary>The game of the open lobby, null when there is none.</summary>
    public string? GameId { get; init; }

    public bool Equals(AppStatus? other) =>
        other is not null && State == other.State && LobbyId == other.LobbyId && RttMs == other.RttMs && GameId == other.GameId
        && Players.SequenceEqual(other.Players);

    public override int GetHashCode() => HashCode.Combine(State, LobbyId, RttMs, Players.Count);
}
