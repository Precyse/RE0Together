using CoopLauncher;
using CoopLauncher.Gui;

namespace LauncherTests;

/// <summary>The words of a refusal.</summary>
public static class StatusTextTests
{
    private static readonly Dictionary<RefusalKind, string> RefusalWords = new()
    {
        [RefusalKind.LauncherBuild] = "Launcher build differs",
        [RefusalKind.GameBuild] = "Game version differs",
        [RefusalKind.ModFiles] = "Mod files differ",
        [RefusalKind.ModNotInstalled] = "Mod not installed",
        [RefusalKind.GameUnsupported] = "Game build unsupported",
        [RefusalKind.GameNotFound] = "Game not found",
        [RefusalKind.RequirementMissing] = "Juiced not found",
        [RefusalKind.UnknownGame] = "Game not in this launcher",
        [RefusalKind.LobbyFull] = "Lobby full",
        [RefusalKind.LobbyClosed] = "Lobby closed",
        [RefusalKind.LobbyNotAllowed] = "Lobby join refused",
        [RefusalKind.LobbyFailed] = "Lobby join failed",
        [RefusalKind.NotCoopLobby] = "Not a co-op lobby",
        [RefusalKind.SteamOffline] = "Steam offline",
        [RefusalKind.SaveTimedOut] = "Save transfer timed out",
        [RefusalKind.SaveFolderMissing] = "Save folder not found",
        [RefusalKind.SaveDiffers] = "Save differs from host's",
        [RefusalKind.HostLeft] = "Host left",
        [RefusalKind.Removed] = "Removed from the lobby",
        [RefusalKind.RejoinGaveUp] = "Could not rejoin",
        [RefusalKind.PortInUse] = "Port 27960 in use",
        [RefusalKind.LaunchFailed] = "Game did not start",
    };

    public static void Run()
    {
        Refusals();
    }

    private static void Refusals()
    {
        foreach (var kind in Enum.GetValues<RefusalKind>())
        {
            var refusal = new Refusal(kind, kind == RefusalKind.PortInUse ? "27960" : "Juiced");
            Check.That(RefusalWords.TryGetValue(kind, out var words) && StatusText.Refusal(refusal) == words, $"refusal words for {kind}");
        }
        Check.That(StatusText.Refusal(new Refusal(RefusalKind.RequirementMissing)) == "Required file not found", "a missing file without a name");
    }
}
