namespace CoopLauncher.Gui;

public static class StatusText
{
    public static string Format(AppStatus status) => status.State switch
    {
        AppState.Idle => "Idle",
        AppState.Connecting => "Connecting",
        AppState.Hosting => "Hosting",
        AppState.Joined => "Joined",
        AppState.GameRunning => "Game running",
        AppState.PeerConnected => status.RttMs is { } rtt ? $"Peer connected {rtt} ms" : "Peer connected",
        _ => status.State.ToString(),
    };
}
