namespace CoopLauncher.Gui;

public static class StatusText
{
    /// <summary>The state words; a refused start (builds or mod files differ) or a problem with the game process (exited, not linked) replaces them.</summary>
    public static string Format(AppStatus status, GameCondition condition = GameCondition.Fine) => condition switch
    {
        GameCondition.Exited => "Game exited",
        GameCondition.Unlinked => "Game not linked",
        _ when status.Problem != BuildMismatch.None => status.Problem switch
        {
            BuildMismatch.LauncherBuild => "Launcher build differs",
            BuildMismatch.GameBuild => "Game version differs",
            BuildMismatch.ModFiles => "Mod files differ",
            _ => "Mod not installed",
        },
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

    /// <summary>Red only once a partner is connected; amber while armed, waiting, when the game has a problem or a start was refused; grey at rest.</summary>
    internal static Color Lamp(AppStatus status, GameCondition condition = GameCondition.Fine) =>
        condition != GameCondition.Fine || status.Problem != BuildMismatch.None ? Theme.Armed
        : status.State switch
        {
            AppState.Idle => Theme.Dim,
            AppState.PeerConnected => Theme.Live,
            _ => Theme.Armed,
        };
}
