using CoopLauncher;

namespace LauncherTests;

/// <summary>Mods follow the package: an installed mod that reads Update is installed again, a running game, a not-installed mod and
/// a modified one are left alone, and one failing install does not stop the next game.</summary>
public static class ModAutoUpdateTests
{
    private const string Dll = "dinput8.dll";

    public static void Run()
    {
        UpdatesAnInstalledModWhoseGameIsClosed();
        SkipsARunningGame();
        SkipsANotInstalledAndAModifiedMod();
        SkipsAGameWithoutAFolder();
        ReportsProgressAndSurvivesAFailingGame();
    }

    private static ModInstallerTests.Rig Installed()
    {
        var rig = new ModInstallerTests.Rig(Dll);
        ModInstaller.Install(rig.Profile, rig.Game);
        return rig;
    }

    private static int Update(ModInstallerTests.Rig rig, Func<GameProfile, bool>? running = null) =>
        ModAutoUpdate.Run([rig.Profile], _ => rig.Game, _ => null, running ?? (_ => false));

    private static void UpdatesAnInstalledModWhoseGameIsClosed()
    {
        using var rig = Installed();
        rig.SetPackage(Dll, "package v2");
        Check.That(rig.State == ModState.UpdateAvailable, "the package moved on: update available");
        Check.That(Update(rig) == 1, "one mod was updated");
        Check.That(rig.ReadGame(Dll) == "package v2" && rig.State == ModState.Installed, "the mod is the package's again");
        Check.That(Update(rig) == 0, "a mod that is current is left alone");
    }

    private static void SkipsARunningGame()
    {
        using var rig = Installed();
        rig.SetPackage(Dll, "package v2");
        Check.That(Update(rig, _ => true) == 0 && rig.ReadGame(Dll) == "package v1 " + Dll, "a running game's mod is not touched");
        Check.That(rig.State == ModState.UpdateAvailable, "it stays an update for when the game closes");
    }

    private static void SkipsANotInstalledAndAModifiedMod()
    {
        using var notInstalled = new ModInstallerTests.Rig(Dll);
        Check.That(Update(notInstalled) == 0 && !notInstalled.GameHas(Dll), "a mod that is not installed stays not installed");
        using var modified = Installed();
        modified.SetGame(Dll, "a replaced file");
        modified.SetPackage(Dll, "package v2");
        Check.That(modified.State == ModState.Modified, "a replaced file reads Modified");
        Check.That(Update(modified) == 0 && modified.ReadGame(Dll) == "a replaced file", "a modified mod waits for the player's repair");
    }

    private static void SkipsAGameWithoutAFolder()
    {
        using var rig = Installed();
        rig.SetPackage(Dll, "package v2");
        Check.That(ModAutoUpdate.Run([rig.Profile], _ => null, _ => null, _ => false) == 0, "a game whose folder is unknown is skipped");
        Check.That(rig.ReadGame(Dll) == "package v1 " + Dll, "and nothing changed");
    }

    private static void ReportsProgressAndSurvivesAFailingGame()
    {
        using var broken = Installed();
        using var good = Installed();
        broken.SetPackage(Dll, "package v2");
        good.SetPackage(Dll, "package v2");
        var reports = new List<ModProgress>();
        int updated;
        using (new FileStream(broken.GamePath(Dll), FileMode.Open, FileAccess.Read, FileShare.Read))
            updated = ModAutoUpdate.Run([broken.Profile with { Id = "broken" }, good.Profile with { Id = "good" }],
                game => game.Id == "broken" ? broken.Game : good.Game, game => game.Id == "good" ? new ListProgress(reports) : null, _ => false);

        Check.That(updated == 1 && good.ReadGame(Dll) == "package v2", "the next game is updated though the first failed");
        Check.That(reports.Count > 0 && reports[^1].Step == ModStep.Done, "the install reports its steps and ends Done");
        Check.That(broken.ReadGame(Dll) == "package v1 " + Dll, "the failed game keeps its installed mod");
    }

    private sealed class ListProgress(List<ModProgress> reports) : IProgress<ModProgress>
    {
        public void Report(ModProgress value) => reports.Add(value);
    }
}
