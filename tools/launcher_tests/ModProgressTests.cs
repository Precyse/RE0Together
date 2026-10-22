using CoopLauncher;

namespace LauncherTests;

/// <summary>The progress of an install: the byte-weighted percent, the reporter (monotonic, 100 only on success), the rate bound, and the
/// steps ModInstaller reports on temp folders.</summary>
public static class ModProgressTests
{
    private sealed class Collected : IProgress<ModProgress>
    {
        public List<ModProgress> Reports { get; } = [];

        public void Report(ModProgress value) => Reports.Add(value);

        public List<ModStep> Steps => Reports.Select(report => report.Step).Distinct().ToList();
    }

    public static void Run()
    {
        PercentFollowsBytes();
        ReporterIsMonotonicAndFinishesOnlyOnSuccess();
        ThrottleBoundsTheRate();
        InstallReportsItsSteps();
        FailedInstallNeverReachesHundred();
        UninstallReportsItsSteps();
    }

    private static void PercentFollowsBytes()
    {
        Check.That(ProgressMath.Percent(ModStep.Copying, 0, 100) == 15 && ProgressMath.Percent(ModStep.Copying, 100, 100) == 60, "a step's band runs from its start to its end");
        Check.That(ProgressMath.Percent(ModStep.Copying, 90, 100) == 55, "a file of 90 of 100 bytes moves the bar by 90 percent of the band");
        Check.That(ProgressMath.Percent(ModStep.Copying, 500, 100) == 60, "bytes beyond the total stay at the end of the band");
        Check.That(ProgressMath.Percent(ModStep.Verifying, 5, 0) == 60, "a step with no bytes sits at the start of its band");
        var steps = new[] { ModStep.Checking, ModStep.BackingUp, ModStep.Copying, ModStep.Verifying, ModStep.RemovingOld };
        Check.That(steps.Zip(steps.Skip(1)).All(pair => ProgressMath.Band(pair.First).End == ProgressMath.Band(pair.Second).Start), "the install bands follow each other");
        Check.That(Enum.GetValues<ModStep>().Where(step => step is not (ModStep.Done or ModStep.Failed)).All(step => ProgressMath.Band(step).End < ProgressMath.Complete), "only Done is at 100");
    }

    private static void ReporterIsMonotonicAndFinishesOnlyOnSuccess()
    {
        var sink = new Collected();
        var report = new ProgressReporter(sink);
        report.Begin(ModStep.Checking, 100, 2);
        report.Advance(90);
        report.Advance(10);
        report.Begin(ModStep.BackingUp, 0, 0);
        report.Advance(5);
        report.Begin(ModStep.Copying, 50, 1);
        report.Advance(50);
        report.Finish();

        Check.That(!sink.Reports.Any(r => r.Step == ModStep.BackingUp), "a step with no files is not reported");
        Check.That(sink.Reports.Zip(sink.Reports.Skip(1)).All(pair => pair.Second.Percent >= pair.First.Percent), "the percent never goes down");
        Check.That(sink.Reports.Where(r => r.Percent == 100).Select(r => r.Step).SequenceEqual([ModStep.Done]), "only Done reports 100");
        Check.That(sink.Reports.Last().Step == ModStep.Done, "a successful run ends with Done");
        var checking = sink.Reports.Where(r => r.Step == ModStep.Checking).ToList();
        Check.That(checking.Last() is { File: 2, Files: 2 } && checking[1].Percent == 9, "file i of n and the byte weight (90 of 100 in a band of 10) are reported");

        var failed = new Collected();
        var failing = new ProgressReporter(failed);
        failing.Begin(ModStep.Copying, 100, 4);
        failing.Advance(50);
        failing.Fail("disk full");
        Check.That(failed.Reports.Last() is { Step: ModStep.Failed, Reason: "disk full" } last && last.Percent < 100, "a failed run keeps its percent below 100 and says why");
        Check.That(failed.Reports.All(r => r.Percent < 100), "a failed run never reaches 100");

        new ProgressReporter(null).Begin(ModStep.Checking, 10, 1);
    }

    private static void ThrottleBoundsTheRate()
    {
        var sink = new Collected();
        long now = 0;
        var throttle = new ThrottledProgress(sink, () => now);
        void send(ModStep step, int percent, long at)
        {
            now = at;
            throttle.Report(new ModProgress(step, percent, 0, 0));
        }

        send(ModStep.Copying, 15, 0);
        send(ModStep.Copying, 16, 10);
        send(ModStep.Copying, 17, 49);
        send(ModStep.Copying, 18, 50);
        send(ModStep.Copying, 18, 200);
        send(ModStep.Verifying, 60, 201);
        send(ModStep.Done, 100, 202);
        Check.That(sink.Reports.Select(r => r.Percent).SequenceEqual([15, 18, 60, 100]),
            "reports within 50 ms are dropped, a change of step and Done always pass, an unchanged percent is not repeated");
    }

    private static ModInstallerTests.Rig PackageOf(out GameProfile profile, params string[] names)
    {
        var rig = new ModInstallerTests.Rig(names);
        foreach (var name in names) rig.SetPackage(name, new string('x', name.Length * 100));
        profile = rig.Profile;
        return rig;
    }

    private static void InstallReportsItsSteps()
    {
        using var rig = PackageOf(out var profile, "a.dll", "bb.dll", "ccc.dll");
        var fresh = new Collected();
        Check.That(ModInstaller.Install(profile, rig.Game, fresh), "the install succeeds");
        Check.That(fresh.Steps.SequenceEqual([ModStep.Checking, ModStep.Copying, ModStep.Verifying, ModStep.Done]), "a first install: check, copy, verify, done");
        Check.That(fresh.Reports.Zip(fresh.Reports.Skip(1)).All(pair => pair.Second.Percent >= pair.First.Percent), "the install percent never goes down");
        Check.That(fresh.Reports.Count(r => r.Percent == 100) == 1 && fresh.Reports.Last() is { Step: ModStep.Done, Percent: 100 }, "100 is reported once, by Done");
        Check.That(fresh.Reports.Last(r => r.Step == ModStep.Copying) is { File: 3, Files: 3 }, "the copy reports file 3 of 3");

        rig.SetGame("a.dll", "the player's");
        File.Delete(rig.GamePath("bb.dll.cfown"));
        var again = new Collected();
        ModInstaller.Install(profile, rig.Game, again);
        Check.That(again.Steps.Contains(ModStep.BackingUp), "a file of the player's in the way adds the backup step");

        var upToDate = new Collected();
        ModInstaller.Install(profile, rig.Game, upToDate);
        Check.That(upToDate.Steps.SequenceEqual([ModStep.Checking, ModStep.Done]), "an install with nothing to copy only checks and finishes");
    }

    private static void FailedInstallNeverReachesHundred()
    {
        using var rig = PackageOf(out var profile, "a.dll", "b.dll");
        ModInstaller.Install(profile, rig.Game);
        rig.SetPackage("a.dll", "new a");
        rig.SetPackage("b.dll", "new b");
        var failed = new Collected();
        using (new FileStream(rig.GamePath("b.dll"), FileMode.Open, FileAccess.Read, FileShare.Read))
            Check.That(!ModInstaller.Install(profile, rig.Game, failed), "an install that cannot replace a file fails");
        Check.That(failed.Reports.Last() is { Step: ModStep.Failed, Reason.Length: > 0 } last && last.Percent < 100, "it ends with Failed, a reason and less than 100");
        Check.That(failed.Reports.All(r => r.Step != ModStep.Done && r.Percent < 100), "no Done and no 100 on the way");

        using var empty = new ModInstallerTests.Rig("a.dll");
        File.Delete(Path.Combine(empty.Package, "a.dll"));
        var missing = new Collected();
        Check.That(!ModInstaller.Install(empty.Profile, empty.Game, missing) && missing.Reports.Single().Step == ModStep.Failed, "a package without the file fails at once");
    }

    private static void UninstallReportsItsSteps()
    {
        using var rig = PackageOf(out var profile, "a.dll", "b.dll");
        ModInstaller.Install(profile, rig.Game);
        var removed = new Collected();
        Check.That(ModInstaller.Uninstall(profile, rig.Game, removed), "the uninstall succeeds");
        Check.That(removed.Steps.SequenceEqual([ModStep.Removing, ModStep.Done]), "uninstall: remove, done");
        Check.That(removed.Reports.Zip(removed.Reports.Skip(1)).All(pair => pair.Second.Percent >= pair.First.Percent) && removed.Reports.Last().Percent == 100,
            "the uninstall percent rises to 100 at Done");
    }
}
