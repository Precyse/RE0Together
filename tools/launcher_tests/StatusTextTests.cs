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
        [RefusalKind.RequirementOtherVersion] = "Juiced is another version",
        [RefusalKind.HostUnreachable] = "Can't reach Juiced through Steam",
        [RefusalKind.GameRunning] = "Close Juiced, then start again",
        [RefusalKind.SaveResignFailed] = "Juiced could not be converted to your Steam account",
        [RefusalKind.FrameworkNotLoaded] = "Juiced did not load",
        [RefusalKind.ModNotLoaded] = "The co-op mod did not load",
        [RefusalKind.SaveNotProtected] = "The mod could not protect your save: quit to the title",
        [RefusalKind.OtherCoopMod] = "Another co-op mod (Juiced) is installed",
        [RefusalKind.ModNotStarted] = "The co-op mod could not start: Juiced",
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
        Check.That(StatusText.Refusal(new Refusal(RefusalKind.HostUnreachable), "Amy") == "Can't reach Amy through Steam", "the unreachable host is named");
        Check.That(StatusText.Refusal(new Refusal(RefusalKind.LauncherBuild, RefusalVersions.Format(7, 5))) == "Your launcher is out of date (host 7, you 5)", "a guest with the older launcher");
        Check.That(StatusText.Refusal(new Refusal(RefusalKind.LauncherBuild, RefusalVersions.Format(5, 7))) == "Host needs launcher 7 (host 5, you 7)", "a host with the older launcher");
        Check.That(StatusText.Refusal(new Refusal(RefusalKind.GameBuild, RefusalVersions.Format(9, 7))) == "Your game version is older than the host's: update it in Steam", "a guest with the older game");
        Check.That(StatusText.Refusal(new Refusal(RefusalKind.GameBuild, RefusalVersions.Format(7, 9)), "Amy") == "Amy has an older game version: update it in Steam", "a host with the older game is named");
        Check.That(StatusText.Refusal(new Refusal(RefusalKind.GameUnsupported, "123", "dd2")) == "Dragon's Dogma 2 was updated by Steam (build 123); this mod supports 26256634", "an unsupported build says both builds");
        Check.That(StatusText.RefusalOf(new Refusal(RefusalKind.SaveResignFailed, "data000.bin"), "Amy") == "Amy could not use the save", "the host's card for a save that could not be converted");
        Check.That(StatusText.RefusalOf(new Refusal(RefusalKind.GameBuild, RefusalVersions.Format(9, 7)), "Amy") == "Amy has an older game version: update it in Steam", "the host's card for a guest with the older game");
        Check.That(StatusText.RefusalOf(new Refusal(RefusalKind.LauncherBuild, RefusalVersions.Format(9, 7)), "Amy") == "Amy needs launcher 9", "the host's card for a guest with the older launcher");
    }
}
