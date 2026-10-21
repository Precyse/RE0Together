using CoopLauncher;

namespace LauncherTests;

/// <summary>ModInstaller on temp folders: ownership by hash, numbered backups of foreign files, Modified and its repair, uninstall,
/// and the all-or-nothing install.</summary>
public static class ModInstallerTests
{
    private const string AdapterIni = "coop/adapter.ini";
    private const string Dll = "dinput8.dll";

    private sealed class Rig : IDisposable
    {
        public Rig(params string[] dsts)
        {
            Root = Path.Combine(Path.GetTempPath(), "cf_mod_" + Guid.NewGuid().ToString("N"));
            Package = Path.Combine(Root, "package");
            Game = Path.Combine(Root, "game");
            Directory.CreateDirectory(Package);
            Directory.CreateDirectory(Game);
            Profile = new GameProfile("t", "Test", 1, "cf_test_not_running.exe", 2, 1,
                dsts.Select(dst => new AdapterFile(Path.Combine(Package, dst), dst)).ToList(), AdapterIni);
            foreach (var dst in dsts) SetPackage(dst, "package v1 " + dst);
        }

        public string Root { get; }
        public string Package { get; }
        public string Game { get; }
        public GameProfile Profile { get; }

        public string GamePath(string relative) => Path.Combine(Game, relative);

        public void SetPackage(string dst, string text) => File.WriteAllText(Path.Combine(Package, dst), text);

        public void SetGame(string relative, string text)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(GamePath(relative))!);
            File.WriteAllText(GamePath(relative), text);
        }

        public string ReadGame(string relative) => File.ReadAllText(GamePath(relative));

        public bool GameHas(string relative) => File.Exists(GamePath(relative)) || Directory.Exists(GamePath(relative));

        public ModState State => ModInstaller.Status(Profile, Game).State;

        public void Dispose() => Directory.Delete(Root, recursive: true);
    }

    public static void Run()
    {
        InstallOverForeignFile();
        InterruptedInstallIsOurs();
        ForeignReplacementIsModifiedAndRepairedWithNumberedBackup();
        UpdateAvailableThenInstalled();
        UninstallKeepsArchiveAndLeavesGameRoot();
        PartialInstallRollsBack();
        AnotherModsFileSurvivesUninstall();
    }

    private static void InstallOverForeignFile()
    {
        using var rig = new Rig(Dll);
        rig.SetGame(Dll, "foreign");
        Check.That(rig.State == ModState.NotInstalled, "a foreign file is not an installed mod");

        Check.That(ModInstaller.Install(rig.Profile, rig.Game), "install over a foreign file succeeds");
        Check.That(rig.ReadGame(Dll) == "package v1 " + Dll, "the package copy is in place");
        Check.That(rig.ReadGame(Dll + ".cfbak") == "foreign", "the foreign file is kept as .cfbak");
        Check.That(rig.State == ModState.Installed, "installed after the install");
        Check.That(!rig.GameHas(Dll + ".tmp") && !rig.GameHas(Dll + ".cfown.tmp") && !rig.GameHas(Dll + ".prev"), "no staging leftovers");
    }

    private static void InterruptedInstallIsOurs()
    {
        using var rig = new Rig(Dll);
        rig.SetGame(Dll, "package v1 " + Dll);
        Check.That(rig.State == ModState.NotInstalled, "a copied file without a marker is not installed yet");

        Check.That(ModInstaller.Install(rig.Profile, rig.Game), "install after an interrupted install succeeds");
        Check.That(!rig.GameHas(Dll + ".cfbak"), "a file equal to the package copy is ours: no backup");
        Check.That(rig.State == ModState.Installed, "installed once the marker is written");
    }

    private static void ForeignReplacementIsModifiedAndRepairedWithNumberedBackup()
    {
        using var rig = new Rig(Dll);
        rig.SetGame(Dll, "original");
        ModInstaller.Install(rig.Profile, rig.Game);

        rig.SetGame(Dll, "reshade");
        Check.That(rig.State == ModState.Modified, "another mod replacing the file after the install: Modified");

        Check.That(ModInstaller.Install(rig.Profile, rig.Game), "install repairs a modified mod");
        Check.That(rig.State == ModState.Installed, "installed after the repair");
        Check.That(rig.ReadGame(Dll + ".cfbak") == "original", "the first backup is not overwritten");
        Check.That(rig.ReadGame(Dll + ".cfbak2") == "reshade", "the foreign replacement is kept as a numbered backup");

        Check.That(ModInstaller.Uninstall(rig.Profile, rig.Game), "uninstall succeeds");
        Check.That(rig.ReadGame(Dll) == "original", "uninstall restores the original file");
        Check.That(rig.ReadGame(Dll + ".cfbak2") == "reshade", "uninstall keeps the numbered backup");
        Check.That(!rig.GameHas(Dll + ".cfown") && !rig.GameHas(Dll + ".cfbuild"), "uninstall removes the marker and the build record");
        Check.That(rig.State == ModState.NotInstalled, "not installed after the uninstall");
    }

    private static void UpdateAvailableThenInstalled()
    {
        using var rig = new Rig(Dll);
        ModInstaller.Install(rig.Profile, rig.Game);
        rig.SetPackage(Dll, "package v2");
        Check.That(rig.State == ModState.UpdateAvailable, "a changed package copy: update available");
        Check.That(ModInstaller.Install(rig.Profile, rig.Game), "the update installs");
        Check.That(rig.ReadGame(Dll) == "package v2" && !rig.GameHas(Dll + ".cfbak"), "the update replaces our own file without a backup");
        Check.That(rig.State == ModState.Installed, "installed after the update");
    }

    private static void UninstallKeepsArchiveAndLeavesGameRoot()
    {
        using var rig = new Rig(Dll);
        ModInstaller.Install(rig.Profile, rig.Game);
        rig.SetGame("coop/adapter.ini", "coop=0");
        rig.SetGame("coop/session/data0.bin", "x");
        rig.SetGame("coop/archive/20260101-000000/data0.bin", "progress");
        rig.SetGame("coop/adapter.log", "log");
        rig.SetGame("Game.exe", "the game");

        ModInstaller.Uninstall(rig.Profile, rig.Game);
        Check.That(rig.ReadGame("coop/archive/20260101-000000/data0.bin") == "progress", "uninstall keeps the archive");
        Check.That(!rig.GameHas("coop/adapter.ini") && !rig.GameHas("coop/session") && !rig.GameHas("coop/adapter.log"),
            "uninstall deletes the rest of the coop folder");
        Check.That(rig.GameHas("Game.exe"), "uninstall leaves the game's own files");

        using var flat = new Rig(Dll);
        var flatProfile = flat.Profile with { AdapterIni = "adapter.ini" };
        ModInstaller.Install(flatProfile, flat.Game);
        flat.SetGame("adapter.ini", "coop=0");
        flat.SetGame("Game.exe", "the game");
        ModInstaller.Uninstall(flatProfile, flat.Game);
        Check.That(flat.GameHas("adapter.ini") && flat.GameHas("Game.exe"), "an ini in the game folder itself does not make the game folder a coop folder");
    }

    private static void PartialInstallRollsBack()
    {
        using var rig = new Rig("a.dll", "b.dll");
        rig.SetGame("a.dll", "foreign a");
        ModInstaller.Install(rig.Profile, rig.Game);
        rig.SetPackage("a.dll", "package v2 a");
        rig.SetPackage("b.dll", "package v2 b");

        bool installed;
        using (new FileStream(rig.GamePath("b.dll"), FileMode.Open, FileAccess.Read, FileShare.Read))
            installed = ModInstaller.Install(rig.Profile, rig.Game);

        Check.That(!installed, "an install that cannot replace one file fails");
        Check.That(rig.ReadGame("a.dll") == "package v1 a.dll" && rig.ReadGame("b.dll") == "package v1 b.dll", "the files already moved are put back");
        Check.That(rig.State == ModState.UpdateAvailable, "the mod is as it was before the failed install");
        Check.That(Directory.GetFiles(rig.Game, "*.tmp").Length == 0 && Directory.GetFiles(rig.Game, "*.prev").Length == 0, "no staging leftovers after the rollback");

        using var foreign = new Rig("a.dll", "b.dll");
        foreign.SetGame("a.dll", "foreign a");
        foreign.SetGame("b.dll", "foreign b");
        using (new FileStream(foreign.GamePath("b.dll"), FileMode.Open, FileAccess.Read, FileShare.Read))
            ModInstaller.Install(foreign.Profile, foreign.Game);
        Check.That(foreign.ReadGame("a.dll") == "foreign a" && !foreign.GameHas("a.dll.cfbak") && !foreign.GameHas("a.dll.cfown"),
            "a failed install leaves the player's file and makes no backup or marker");
    }

    private static void AnotherModsFileSurvivesUninstall()
    {
        using var rig = new Rig(Dll);
        ModInstaller.Install(rig.Profile, rig.Game);
        rig.SetGame(Dll, "reshade");
        ModInstaller.Uninstall(rig.Profile, rig.Game);
        Check.That(rig.ReadGame(Dll) == "reshade", "uninstall does not delete a file another mod put in place");
        Check.That(!rig.GameHas(Dll + ".cfown"), "the stale marker is removed");
    }
}
