namespace CoopLauncher.Gui;

public static class StatusText
{
    public static string Format(AppStatus status) => status.State switch
    {
        AppState.Idle => "Idle",
        AppState.Offline => "Offline",
        AppState.Connecting => "Connecting",
        AppState.Hosting => "Hosting",
        AppState.Joined => "Joined",
        AppState.GameRunning => "Game running",
        AppState.PeerConnected => status.RttMs is { } rtt ? $"Connected {rtt} ms" : "Connected",
        _ => status.State.ToString(),
    };

    /// <summary>Red only once a partner is connected; amber while armed or waiting; grey at rest.</summary>
    internal static Color Lamp(AppState state) => state switch
    {
        AppState.Idle => Theme.Dim,
        AppState.PeerConnected => Theme.Live,
        _ => Theme.Armed,
    };
}
