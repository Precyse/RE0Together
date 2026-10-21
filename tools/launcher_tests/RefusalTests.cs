using CoopLauncher;

namespace LauncherTests;

/// <summary>The maps from build mismatches and Steam's answers to refusal kinds.</summary>
public static class RefusalTests
{
    public static void Run()
    {
        Builds();
        Entering();
        Creating();
    }

    private static void Builds()
    {
        Check.That(Refusals.FromBuildMismatch(BuildMismatch.LauncherBuild) == RefusalKind.LauncherBuild, "launcher build");
        Check.That(Refusals.FromBuildMismatch(BuildMismatch.GameBuild) == RefusalKind.GameBuild, "game build");
        Check.That(Refusals.FromBuildMismatch(BuildMismatch.ModFiles) == RefusalKind.ModFiles, "mod files");
        Check.That(Refusals.FromBuildMismatch(BuildMismatch.ModNotInstalled) == RefusalKind.ModNotInstalled, "mod not installed");
        var threw = false;
        try
        {
            Refusals.FromBuildMismatch(BuildMismatch.None);
        }
        catch (ArgumentOutOfRangeException)
        {
            threw = true;
        }
        Check.That(threw, "no mismatch is not a refusal");
    }

    private static void Entering()
    {
        Check.That(Refusals.FromEnterResponse(2) == RefusalKind.LobbyClosed, "enter 2 (doesn't exist): lobby closed");
        Check.That(Refusals.FromEnterResponse(4) == RefusalKind.LobbyFull, "enter 4 (full): lobby full");
        Check.That(Refusals.FromEnterResponse(3) == RefusalKind.LobbyNotAllowed, "enter 3 (not allowed): join refused");
        for (uint response = 6; response <= 11; response++)
            Check.That(Refusals.FromEnterResponse(response) == RefusalKind.LobbyNotAllowed, $"enter {response} (banned, limited, blocked ...): join refused");
        Check.That(Refusals.FromEnterResponse(5) == RefusalKind.LobbyFailed, "enter 5 (error): join failed");
        Check.That(Refusals.FromEnterResponse(0) == RefusalKind.LobbyFailed, "enter 0: join failed");
        Check.That(Refusals.FromEnterResponse(12) == RefusalKind.LobbyFailed, "an unknown response: join failed");
    }

    private static void Creating()
    {
        foreach (var offline in new[] { 3, 16, 20, 2 })
            Check.That(Refusals.FromCreateResult(offline) == RefusalKind.SteamOffline, $"result {offline}: Steam offline");
        Check.That(Refusals.FromCreateResult(25) == RefusalKind.LobbyFailed, "limit exceeded: lobby failed");
        Check.That(Refusals.FromCreateResult(0) == RefusalKind.LobbyFailed, "result 0: lobby failed");
    }
}
