using CoopLauncher;

namespace LauncherTests;

/// <summary>The component updater on temp folders and a folder standing in for the release: only changed components are
/// downloaded, a mods-only update needs no restart, and a hash mismatch or a failed replace leaves every installed file as it was.</summary>
public static class ComponentUpdateTests
{
    private const string V1Re0 = "v1 " + ReleaseFixture.Re0Dll;
    private const string V1Ds2 = "v1 " + ReleaseFixture.Ds2Dll;
    private const string V1Exe = "v1 " + ReleaseFixture.LauncherExe;

    public static void Run()
    {
        PlanNamesOnlyTheChangedFiles();
        ModsOnlyUpdateDownloadsOneComponentAndNeedsNoRestart();
        LauncherChangeNeedsARestart();
        ProfileChangeNeedsARestart();
        MatchingFilesOnlyRecordTheBuild();
        HashMismatchOfTheZipReplacesNothing();
        HashMismatchOfAFileInsideReplacesNothing();
        InterruptedReplaceIsRolledBack();
        HalfUpdatedPackageHeals();
    }

    private static ReleaseFixture Installed(out ReleaseManifest first)
    {
        var fixture = new ReleaseFixture();
        first = fixture.Publish();
        fixture.InstallPackage();
        return fixture;
    }

    private static bool Applies(ReleaseFixture fixture, ReleaseManifest manifest, out bool restart, ReleaseFixture.AssetFolder? source = null)
    {
        restart = false;
        try
        {
            restart = Updater.Apply(manifest, source ?? fixture.Source, fixture.Root, new ProgressReporter(null));
            return true;
        }
        catch (Exception e) when (e is InvalidDataException or IOException)
        {
            return false;
        }
    }

    private static void PlanNamesOnlyTheChangedFiles()
    {
        using var fixture = Installed(out var first);
        Check.That(ComponentUpdate.Plan(first, fixture.Root).IsEmpty, "an installed package matches its own manifest: nothing to do");
        fixture.Set(ReleaseFixture.Re0Dll, "v2 re0");
        var plan = ComponentUpdate.Plan(fixture.Publish(), fixture.Root);
        Check.That(plan.Components.Select(c => c.Id).SequenceEqual(["re0"]), "only the component with a different file is planned");
        Check.That(plan.Files.Select(f => f.File.Path).SequenceEqual([ReleaseFixture.Re0Dll]), "only the changed file of it, not its unchanged profile");
        File.Delete(Path.Combine(fixture.Root, ReleaseFixture.Ds2Dll));
        Check.That(ComponentUpdate.Plan(first, fixture.Root).Components.Select(c => c.Id).SequenceEqual(["ds2"]), "a missing file counts as changed");
    }

    private static void ModsOnlyUpdateDownloadsOneComponentAndNeedsNoRestart()
    {
        using var fixture = Installed(out _);
        fixture.Set(ReleaseFixture.Re0Dll, "v2 re0");
        var second = fixture.Publish();
        var source = fixture.Source;

        Check.That(Applies(fixture, second, out var restart, source), "the update applies");
        Check.That(!restart, "a mods-only update needs no restart");
        Check.That(source.Requested.SequenceEqual(["re0.zip"]), "only the changed component's zip was downloaded");
        Check.That(fixture.ReadRoot(ReleaseFixture.Re0Dll) == "v2 re0", "the adapter is replaced");
        Check.That(fixture.ReadRoot(ReleaseFixture.LauncherExe) == V1Exe, "the launcher is untouched");
        Check.That(fixture.InstalledBuild() == second.Build, "installed.json holds the new build");
    }

    private static void LauncherChangeNeedsARestart()
    {
        using var fixture = Installed(out _);
        fixture.Set(ReleaseFixture.LauncherExe, "v2 exe");
        var second = fixture.Publish();
        var source = fixture.Source;

        Check.That(Applies(fixture, second, out var restart, source) && restart, "a changed launcher applies and needs the restart");
        Check.That(source.Requested.SequenceEqual(["launcher.zip"]), "only the launcher zip was downloaded");
        Check.That(fixture.ReadRoot(ReleaseFixture.LauncherExe) == "v2 exe" && fixture.InstalledBuild() == second.Build, "the exe and the build are new");
    }

    private static void ProfileChangeNeedsARestart()
    {
        using var fixture = Installed(out _);
        fixture.Set(ReleaseFixture.Re0Profile, "v2 profile");
        Check.That(Applies(fixture, fixture.Publish(), out var restart) && restart, "a changed game profile is read at start, so it needs the restart");
    }

    private static void MatchingFilesOnlyRecordTheBuild()
    {
        using var fixture = Installed(out _);
        var second = fixture.Publish();
        var source = fixture.Source;
        Check.That(Applies(fixture, second, out var restart, source) && !restart, "a release with the same files applies");
        Check.That(source.Requested.Count == 0, "nothing was downloaded");
        Check.That(fixture.InstalledBuild() == second.Build, "the build is recorded");
    }

    private static void HashMismatchOfTheZipReplacesNothing()
    {
        using var fixture = Installed(out var first);
        fixture.Set(ReleaseFixture.Re0Dll, "v2 re0");
        fixture.Set(ReleaseFixture.LauncherExe, "v2 exe");
        var second = fixture.Publish();
        File.AppendAllText(Path.Combine(fixture.Assets, "re0.zip"), "tampered");

        Check.That(!Applies(fixture, second, out _), "a zip that does not match the manifest is refused");
        Check.That(fixture.ReadRoot(ReleaseFixture.Re0Dll) == V1Re0 && fixture.ReadRoot(ReleaseFixture.LauncherExe) == V1Exe,
            "no file of any component was replaced, though the launcher zip was fine");
        Check.That(fixture.InstalledBuild() == first.Build && NoLeftovers(fixture.Root), "installed.json is unchanged and no .old or marker stays");
    }

    private static void HashMismatchOfAFileInsideReplacesNothing()
    {
        using var fixture = Installed(out var first);
        fixture.Set(ReleaseFixture.Re0Dll, "v2 re0");
        var second = fixture.Publish();
        var wrongHash = second.Component("re0")!.Files.Select(f => f.Path == ReleaseFixture.Re0Dll ? f with { Sha256 = new string('0', 64) } : f).ToList();
        var forged = second with { Components = second.Components.Select(c => c.Id == "re0" ? c with { Files = wrongHash } : c).ToList() };

        Check.That(!Applies(fixture, forged, out _), "a file that does not match its manifest hash is refused");
        Check.That(fixture.ReadRoot(ReleaseFixture.Re0Dll) == V1Re0 && fixture.InstalledBuild() == first.Build, "the installed file and build are as before");

        var extra = new ManifestFile("adapters/re0/absent.dll", new string('1', 64), 3);
        var missing = second with { Components = second.Components.Select(c => c.Id == "re0" ? c with { Files = [.. c.Files, extra] } : c).ToList() };
        Check.That(!Applies(fixture, missing, out _), "a file the zip does not carry is refused");
        Check.That(fixture.ReadRoot(ReleaseFixture.Re0Dll) == V1Re0, "still as before");
    }

    private static void InterruptedReplaceIsRolledBack()
    {
        using var fixture = Installed(out var first);
        fixture.Set(ReleaseFixture.Re0Dll, "v2 re0");
        fixture.Set(ReleaseFixture.Ds2Dll, "v2 ds2");
        var second = fixture.Publish();

        bool applied;
        using (new FileStream(Path.Combine(fixture.Root, ReleaseFixture.Ds2Dll), FileMode.Open, FileAccess.Read, FileShare.None))
            applied = Applies(fixture, second, out _);

        Check.That(!applied, "a replace that cannot move a file fails");
        Check.That(fixture.ReadRoot(ReleaseFixture.Re0Dll) == V1Re0 && fixture.ReadRoot(ReleaseFixture.Ds2Dll) == V1Ds2, "every original is back, the ones already moved too");
        Check.That(fixture.InstalledBuild() == first.Build, "installed.json still names the old build");
        Updater.Recover(fixture.Root);
        Check.That(NoLeftovers(fixture.Root), "recover leaves no marker and no .old files");
        Check.That(Applies(fixture, second, out _) && fixture.ReadRoot(ReleaseFixture.Ds2Dll) == "v2 ds2", "the update goes through once the file is free");
    }

    private static void HalfUpdatedPackageHeals()
    {
        using var fixture = Installed(out _);
        fixture.Set(ReleaseFixture.Re0Dll, "v2 re0");
        var second = fixture.Publish();
        File.WriteAllText(Path.Combine(fixture.Root, ReleaseFixture.Re0Dll), "v2 re0");
        var source = fixture.Source;

        Check.That(Applies(fixture, second, out _, source) && source.Requested.Count == 0, "files that already match are not fetched again, whatever installed.json says");
        Check.That(fixture.InstalledBuild() == second.Build, "and the build is recorded");
    }

    private static bool NoLeftovers(string root) =>
        Directory.GetFiles(root, "*.old", SearchOption.AllDirectories).Length == 0 && !File.Exists(Path.Combine(root, "update.pending"));
}
