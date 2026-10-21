using CoopLauncher;

namespace LauncherTests;

/// <summary>Log.RotatePrevious: the log of each earlier run is kept under a numbered name, up to ten runs.</summary>
public static class LogRotationTests
{
    public static void Run()
    {
        var dir = Path.Combine(Path.GetTempPath(), "cf_log_rotation_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(dir);
        try
        {
            var log = Path.Combine(dir, "launcher.log");
            var previous = Path.Combine(dir, "launcher.prev.log");

            Log.RotatePrevious(log, previous);
            Check.That(Directory.GetFiles(dir).Length == 0, "no log yet: nothing to rotate");

            for (var run = 1; run <= Log.KeptRuns + 3; run++)
            {
                File.WriteAllText(log, $"run {run}");
                Log.RotatePrevious(log, previous);
                Check.That(!File.Exists(log), $"run {run}: the current log is moved away");
            }

            Check.That(File.ReadAllText(previous) == $"run {Log.KeptRuns + 3}", "the newest earlier run is launcher.prev.log");
            Check.That(File.ReadAllText(Path.Combine(dir, "launcher.prev2.log")) == $"run {Log.KeptRuns + 2}", "the one before is launcher.prev2.log");
            Check.That(File.ReadAllText(Path.Combine(dir, $"launcher.prev{Log.KeptRuns}.log")) == "run 4", "the oldest kept run is launcher.prev10.log");
            Check.That(Directory.GetFiles(dir).Length == Log.KeptRuns, "only the kept runs remain");
        }
        finally
        {
            Directory.Delete(dir, recursive: true);
        }
    }
}
