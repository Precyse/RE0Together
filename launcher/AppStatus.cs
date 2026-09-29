namespace CoopLauncher;

public enum AppState { Idle, Connecting, Hosting, Joined, GameRunning, PeerConnected }

/// <summary>What the main loop is doing right now, for display.</summary>
public sealed record AppStatus(AppState State, ulong LobbyId = 0, int? RttMs = null)
{
    public static readonly AppStatus Idle = new(AppState.Idle);
}
