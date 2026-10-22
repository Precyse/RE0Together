using CoopLauncher;

namespace LauncherTests;

/// <summary>ModInstaller with a folder entry on temp folders: every file installed with the same rules as a single file, the folder
/// itself holding nothing but the mod's files, an update that removes a dropped file, uninstall restoring a backup, Modified.</summary>
public static class ModFolderTests
{
    private const string Dll = "dinput8.dll";
    private const string Scripts = "coop/scripts";
    private const string Bookkeeping = "coop/.cfmod/scripts";

    public static void Run()
    {
        InstallPutsEveryFileInAndKeepsTheFolderClean();
        ContentHashCoversTheFolder();
        UpdateRemovesADroppedFile();
        AnOlderInstallWithoutScriptsOffersAnUpdate();
        ADroppedFileThePlayerChangedStays();
        UninstallRestoresABackupAndRemovesTheRest();
        ModifiedFileIsRepairedWithNumberedBackup();
        MissingFolderIsAnIncompletePackage();
        FailedUpdateRestoresEverything();
    }

    private static ModInstallerTests.Rig NewRig(out GameProfile profile, params string[] scripts)
    {
        var rig = new ModInstallerTests.Rig(Dll);
        foreach (var script in scripts) rig.SetPackage("scripts/" + script, "package v1 " + script);
        profile = Profile(rig);
        return rig;
    }

    private static GameProfile Profile(ModInstallerTests.Rig rig) =>
        rig.Profile with
        {
            AdapterFiles = ModFiles.Expand([new AdapterFile(Dll, Dll), new AdapterFile("scripts", Scripts, Folder: true)], rig.Package)
                .Select(f => f with { Src = Path.Combine(rig.Package, f.Src) }).ToList(),
        };

    private static string ScriptPath(string name) => Scripts + "/" + name;

    private static List<string> FolderFiles(ModInstallerTests.Rig rig) =>
        Directory.GetFiles(rig.GamePath(Scripts)).Select(Path.GetFileName).Order(StringComparer.Ordinal).ToList()!;

    private static void InstallPutsEveryFileInAndKeepsTheFolderClean()
    {
        using var rig = NewRig(out var profile, "a.lua", "b.lua", "sub/c.lua");
        Check.That(profile.AdapterFiles.Count == 4, "the dll and three scripts");
        Check.That(ModInstaller.Status(profile, rig.Game).State == ModState.NotInstalled, "nothing installed yet");

        Check.That(ModInstaller.Install(profile, rig.Game), "the install succeeds");
        Check.That(rig.ReadGame(ScriptPath("a.lua")) == "package v1 a.lua" && rig.ReadGame(ScriptPath("sub/c.lua")) == "package v1 sub/c.lua",
            "every file of the folder, subfolders too, is in place");
        Check.That(FolderFiles(rig).SequenceEqual(["a.lua", "b.lua"]), "the game's scripts folder holds only the mod's scripts: no marker, backup or manifest");
        Check.That(rig.GameHas(Bookkeeping + "/a.lua.cfown") && rig.GameHas(Bookkeeping + "/.cfmanifest"), "markers and the manifest sit beside the folder");
        Check.That(ModFiles.ParseManifest(rig.ReadGame(Bookkeeping + "/.cfmanifest")).SequenceEqual([ScriptPath("a.lua"), ScriptPath("b.lua"), ScriptPath("sub/c.lua")]),
            "the manifest lists the three scripts");
        Check.That(ModInstaller.Status(profile, rig.Game).State == ModState.Installed, "installed");
        Check.That(!rig.GameHas(Dll + ".tmp") && !rig.GameHas(Bookkeeping + "/a.lua.tmp") && !rig.GameHas(Bookkeeping + "/a.lua.prev"), "no staging leftovers");
    }

    private static void ContentHashCoversTheFolder()
    {
        using var rig = NewRig(out var profile, "a.lua", "b.lua");
        Check.That(ModInstaller.ContentHash(profile, rig.Game) == null, "not installed: no hash");
        ModInstaller.Install(profile, rig.Game);
        var hash = ModInstaller.ContentHash(profile, rig.Game);
        rig.SetGame(ScriptPath("b.lua"), "changed");
        var changed = ModInstaller.ContentHash(profile, rig.Game);
        Check.That(hash != null && changed != null && !hash.SequenceEqual(changed), "a changed script changes the hash compared between players");
        File.Delete(rig.GamePath(ScriptPath("a.lua")));
        Check.That(ModInstaller.ContentHash(profile, rig.Game) == null, "a missing script: no hash");
    }

    private static void UpdateRemovesADroppedFile()
    {
        using var rig = NewRig(out var profile, "a.lua", "b.lua", "old.lua");
        ModInstaller.Install(profile, rig.Game);

        File.Delete(Path.Combine(rig.Package, "scripts", "old.lua"));
        rig.SetPackage("scripts/a.lua", "package v2 a");
        rig.SetPackage("scripts/new.lua", "package v2 new");
        var updated = Profile(rig);
        Check.That(ModInstaller.Status(updated, rig.Game).State == ModState.UpdateAvailable, "a changed script and a dropped one: update available");

        Check.That(ModInstaller.Install(updated, rig.Game), "the update installs");
        Check.That(FolderFiles(rig).SequenceEqual(["a.lua", "b.lua", "new.lua"]), "the dropped script is gone, the new one is in");
        Check.That(rig.ReadGame(ScriptPath("a.lua")) == "package v2 a", "the changed script is replaced");
        Check.That(!rig.GameHas(Bookkeeping + "/old.lua.cfown"), "the dropped script's marker is gone");
        Check.That(ModFiles.ParseManifest(rig.ReadGame(Bookkeeping + "/.cfmanifest")).SequenceEqual([ScriptPath("a.lua"), ScriptPath("b.lua"), ScriptPath("new.lua")]),
            "the manifest follows the package");
        Check.That(ModInstaller.Status(updated, rig.Game).State == ModState.Installed, "installed after the update");

        using var onlyDropped = NewRig(out var before, "a.lua", "old.lua");
        ModInstaller.Install(before, onlyDropped.Game);
        File.Delete(Path.Combine(onlyDropped.Package, "scripts", "old.lua"));
        Check.That(ModInstaller.Status(Profile(onlyDropped), onlyDropped.Game).State == ModState.UpdateAvailable, "only a dropped script: still an update");
    }

    private static void AnOlderInstallWithoutScriptsOffersAnUpdate()
    {
        using var rig = NewRig(out var withScripts, "a.lua", "b.lua");
        ModInstaller.Install(rig.Profile, rig.Game);
        Check.That(ModInstaller.Status(withScripts, rig.Game).State == ModState.UpdateAvailable, "a mod installed before the scripts were added: update available");
        ModInstaller.Install(withScripts, rig.Game);
        Check.That(ModInstaller.Status(withScripts, rig.Game).State == ModState.Installed, "installed once the scripts are in");
        File.Delete(rig.GamePath(ScriptPath("a.lua")));
        Check.That(ModInstaller.Status(withScripts, rig.Game).State == ModState.NotInstalled, "a script that went missing after the install: not installed");
    }

    private static void ADroppedFileThePlayerChangedStays()
    {
        using var rig = NewRig(out var profile, "a.lua", "old.lua");
        ModInstaller.Install(profile, rig.Game);
        rig.SetGame(ScriptPath("old.lua"), "the player's own");
        File.Delete(Path.Combine(rig.Package, "scripts", "old.lua"));
        ModInstaller.Install(Profile(rig), rig.Game);
        Check.That(rig.ReadGame(ScriptPath("old.lua")) == "the player's own", "a dropped file the player changed is left in place");
    }

    private static void UninstallRestoresABackupAndRemovesTheRest()
    {
        using var rig = NewRig(out var profile, "a.lua", "b.lua", "sub/c.lua");
        rig.SetGame(ScriptPath("a.lua"), "foreign a");
        rig.SetGame("coop/sr2_adapter.ini", "coop=1");
        ModInstaller.Install(profile, rig.Game);
        Check.That(rig.ReadGame(Bookkeeping + "/a.lua.cfbak") == "foreign a", "the foreign script is backed up beside the folder");
        Check.That(rig.ReadGame(ScriptPath("a.lua")) == "package v1 a.lua", "the package script replaced it");

        Check.That(ModInstaller.Uninstall(profile, rig.Game), "uninstall succeeds");
        Check.That(rig.ReadGame(ScriptPath("a.lua")) == "foreign a", "uninstall restores the foreign script");
        Check.That(FolderFiles(rig).SequenceEqual(["a.lua"]), "only the restored script is left in the folder");
        Check.That(!rig.GameHas(ScriptPath("sub")) && !rig.GameHas(Dll), "the mod's other files and empty folders are removed");
        Check.That(!rig.GameHas("coop/.cfmod") && !rig.GameHas("coop/sr2_adapter.ini"), "bookkeeping and the coop folder's content are gone");
        Check.That(ModInstaller.Status(profile, rig.Game).State == ModState.NotInstalled, "not installed after the uninstall");

        using var plain = NewRig(out var plainProfile, "a.lua");
        ModInstaller.Install(plainProfile, plain.Game);
        ModInstaller.Uninstall(plainProfile, plain.Game);
        Check.That(!plain.GameHas(Scripts) && !plain.GameHas("coop/.cfmod"), "with no foreign file the whole scripts folder goes");
    }

    private static void ModifiedFileIsRepairedWithNumberedBackup()
    {
        using var rig = NewRig(out var profile, "a.lua", "b.lua");
        ModInstaller.Install(profile, rig.Game);
        rig.SetGame(ScriptPath("b.lua"), "edited by hand");
        Check.That(ModInstaller.Status(profile, rig.Game).State == ModState.Modified, "an edited script: modified");

        Check.That(ModInstaller.Install(profile, rig.Game), "install repairs it");
        Check.That(rig.ReadGame(ScriptPath("b.lua")) == "package v1 b.lua", "the package script is back");
        Check.That(rig.ReadGame(Bookkeeping + "/b.lua.cfbak") == "edited by hand", "the edit is kept as a backup");
        Check.That(ModInstaller.Status(profile, rig.Game).State == ModState.Installed, "installed after the repair");
    }

    private static void MissingFolderIsAnIncompletePackage()
    {
        using var rig = NewRig(out var profile);
        Check.That(profile.AdapterFiles.Count == 2, "the dll and the folder as one entry");
        Check.That(!ModInstaller.Install(profile, rig.Game), "an install without the package's scripts fails");
        Check.That(!rig.GameHas(Dll), "and installs nothing");
    }

    private static void FailedUpdateRestoresEverything()
    {
        using var rig = NewRig(out var profile, "a.lua", "b.lua", "old.lua");
        ModInstaller.Install(profile, rig.Game);
        File.Delete(Path.Combine(rig.Package, "scripts", "old.lua"));
        rig.SetPackage("scripts/a.lua", "package v2 a");
        rig.SetPackage("scripts/b.lua", "package v2 b");
        var updated = Profile(rig);

        bool installed;
        using (new FileStream(rig.GamePath(ScriptPath("b.lua")), FileMode.Open, FileAccess.Read, FileShare.Read))
            installed = ModInstaller.Install(updated, rig.Game);

        Check.That(!installed, "an update that cannot replace one script fails");
        Check.That(rig.ReadGame(ScriptPath("a.lua")) == "package v1 a.lua" && rig.ReadGame(ScriptPath("old.lua")) == "package v1 old.lua",
            "the scripts already moved, and the one to remove, are put back");
        Check.That(ModFiles.ParseManifest(rig.ReadGame(Bookkeeping + "/.cfmanifest")).Contains(ScriptPath("old.lua")), "the manifest is as it was");
        Check.That(FolderFiles(rig).SequenceEqual(["a.lua", "b.lua", "old.lua"]), "no staging leftovers in the folder");
    }
}
