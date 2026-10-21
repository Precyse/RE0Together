namespace CoopLauncher;

/// <summary>Offline: the main loop could not start (Steam not running) and is retrying.</summary>
public enum AppState { Idle, Connecting, Hosting, Joined, GameRunning, PeerConnected, Offline }

/// <summary>One lobby member as the window shows it; Status is what its game reports (rendezvous games), else empty.</summary>
public sealed record PlayerSlot(string Name, bool IsHost, bool IsLocal, string Status = "");

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

    /// <summary>Why the last start was refused (builds or mod files differ); shown while idle.</summary>
    public BuildMismatch Problem { get; init; }

    /// <summary>The session has the adapter link (a rendezvous game with the bridge expects its adapter to connect).</summary>
    public bool Bridged { get; init; }

    public bool Equals(AppStatus? other) =>
        other is not null && State == other.State && LobbyId == other.LobbyId && RttMs == other.RttMs && GameId == other.GameId
        && Problem == other.Problem && Bridged == other.Bridged
        && Players.SequenceEqual(other.Players) && SeatCharacters.SequenceEqual(other.SeatCharacters);

    public override int GetHashCode() => HashCode.Combine(State, LobbyId, RttMs, Players.Count);
}
