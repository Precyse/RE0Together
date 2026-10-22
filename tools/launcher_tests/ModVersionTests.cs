using CoopLauncher;

namespace LauncherTests;

/// <summary>A mod records the version the package has for it (the mod component in installed.json) and the log says what changed:
/// installed vN, updated to vN (was vM), the same files at a new version. Needs an installed.json beside the test exe, which it
/// removes again.</summary>
public static class ModVersionTests
{
    private const string Dll = "dinput8.dll";
    private const string RigGameId = "t";

    public static void Run()
    {
        var installedPath = InstalledRelease.PathFor(AppContext.BaseDirectory);
        try
        {
            FreshInstallThenUpdateThenSameFilesAtANewVersion(installedPath);
            ReleaseNumberFromBeforeVersionsIsNotAVersion(installedPath);
            DevBuildHasNoVersionToName(installedPath);
        }
        finally
        {
            File.Delete(installedPath);
        }
    }

    private static void PackageAt(string installedPath, int modVersion) =>
        new InstalledRelease(100, "next", "abc", new Dictionary<string, int> { [InstalledRelease.LauncherId] = 8, [RigGameId] = modVersion }).Write(installedPath);

    private static bool Said(IEnumerable<string> lines, string text) => lines.Any(line => line.EndsWith(text, StringComparison.Ordinal));

    private static void FreshInstallThenUpdateThenSameFilesAtANewVersion(string installedPath)
    {
        using var rig = new ModInstallerTests.Rig(Dll);
        PackageAt(installedPath, 3);
        Check.That(BuildCheck.LocalBuild() == 8 && BuildCheck.LocalModVersion(RigGameId) == 3, "the launcher and the mod read their own versions");

        var fresh = LogCapture.During(() => ModInstaller.Install(rig.Profile, rig.Game));
        Check.That(Said(fresh, "Test mod installed v3") && ModInstaller.Status(rig.Profile, rig.Game).Build == 3, "a fresh install records and names the package's mod version");

        rig.SetPackage(Dll, "package v2");
        PackageAt(installedPath, 4);
        var updated = LogCapture.During(() => ModAutoUpdate.Run([rig.Profile], _ => rig.Game, _ => null, _ => false));
        Check.That(Said(updated, "Test mod updated to v4 (was v3)") && ModInstaller.Status(rig.Profile, rig.Game).Build == 4, "an update names both versions");

        PackageAt(installedPath, 5);
        var same = 0;
        var recorded = LogCapture.During(() => same = ModAutoUpdate.Run([rig.Profile], _ => rig.Game, _ => null, _ => false));
        Check.That(same == 0 && Said(recorded, "Test mod updated to v5 (was v4)") && ModInstaller.Status(rig.Profile, rig.Game).Build == 5,
            "a release that moved the version without changing the files only records it, no install");
        Check.That(rig.ReadGame(Dll) == "package v2" && rig.State == ModState.Installed, "and leaves the files as they were");
        Check.That(LogCapture.During(() => ModAutoUpdate.Run([rig.Profile], _ => rig.Game, _ => null, _ => false)).Count == 0, "a mod at the package's version says nothing");

        var again = LogCapture.During(() => ModInstaller.Install(rig.Profile, rig.Game));
        Check.That(Said(again, "Test mod reinstalled v5"), "installing the same version again is a reinstall");
    }

    private static void ReleaseNumberFromBeforeVersionsIsNotAVersion(string installedPath)
    {
        using var rig = new ModInstallerTests.Rig(Dll);
        PackageAt(installedPath, 3);
        ModInstaller.Install(rig.Profile, rig.Game);
        const int oldReleaseNumber = 118;
        File.WriteAllText(rig.GamePath(Dll + ".cfbuild"), oldReleaseNumber.ToString());
        Check.That(ModInstaller.Status(rig.Profile, rig.Game).Build == 0, "a .cfbuild from before versions (a bare release number) reads as unknown");

        PackageAt(installedPath, 4);
        var recorded = LogCapture.During(() => ModAutoUpdate.Run([rig.Profile], _ => rig.Game, _ => null, _ => false));
        Check.That(Said(recorded, "Test mod updated to v4") && !recorded.Any(line => line.Contains("(was")) && ModInstaller.Status(rig.Profile, rig.Game).Build == 4,
            "the first record over a release number names the version without a 'was'");
    }

    private static void DevBuildHasNoVersionToName(string installedPath)
    {
        File.Delete(installedPath);
        using var rig = new ModInstallerTests.Rig(Dll);
        Check.That(BuildCheck.LocalBuild() == BuildInfo.DevBuild && BuildCheck.LocalModVersion(RigGameId) == BuildInfo.DevBuild, "no installed.json: a dev build");
        var fresh = LogCapture.During(() => ModInstaller.Install(rig.Profile, rig.Game));
        Check.That(Said(fresh, "Test mod installed") && ModInstaller.Status(rig.Profile, rig.Game).Build == 0, "a dev build installs without a version");
        Check.That(!ModInstaller.RecordPackageVersion(rig.Profile, rig.Game), "and has none to record");
    }
}
