namespace CoopLauncher.Gui;

public static class StatusText
{
    /// <summary>The state words; a problem with the game process (exited, not linked) replaces them.</summary>
    public static string Format(AppStatus status, GameCondition condition = GameCondition.Fine) => condition switch
    {
        GameCondition.Exited => "Game exited",
        GameCondition.Unlinked => "Game not linked",
        _ => status.State switch
        {
            AppState.Idle => "Idle",
            AppState.Offline => "Offline",
            AppState.Connecting => "Connecting",
            AppState.Hosting => "Hosting",
            AppState.Joined => "Joined",
            AppState.GameRunning => "Game running",
            AppState.PeerConnected => status.RttMs is { } rtt ? $"Connected {rtt} ms" : "Connected",
            _ => status.State.ToString(),
        },
    };

    /// <summary>Red only once a partner is connected; amber while armed, waiting or when the game has a problem; grey at rest.</summary>
    internal static Color Lamp(AppState state, GameCondition condition = GameCondition.Fine) =>
        condition != GameCondition.Fine ? Theme.Armed
        : state switch
        {
            AppState.Idle => Theme.Dim,
            AppState.PeerConnected => Theme.Live,
            _ => Theme.Armed,
        };
}
