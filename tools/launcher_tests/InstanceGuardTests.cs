using CoopLauncher;

namespace LauncherTests;

/// <summary>InstanceGuard.ShouldClose over every input combination, and the command-line run mutex. No process is started or closed.</summary>
public static class InstanceGuardTests
{
    public static void Run()
    {
        // isCli, holdsWindow, answered, responding -> close
        Rule(isCli: false, holdsWindow: false, answered: false, responding: false, expected: true, "a hung stale copy is closed");
        Rule(isCli: false, holdsWindow: false, answered: false, responding: true, expected: false, "a responding stale copy stays");
        Rule(isCli: false, holdsWindow: false, answered: true, responding: false, expected: true, "a hung stale copy is closed beside a live window");
        Rule(isCli: false, holdsWindow: false, answered: true, responding: true, expected: false, "a responding stale copy beside a live window stays");
        Rule(isCli: false, holdsWindow: true, answered: false, responding: false, expected: true, "a silent window that is hung is closed");
        Rule(isCli: false, holdsWindow: true, answered: false, responding: true, expected: false, "a silent window that still responds stays");
        Rule(isCli: false, holdsWindow: true, answered: true, responding: false, expected: false, "a window that answers is never closed");
        Rule(isCli: false, holdsWindow: true, answered: true, responding: true, expected: false, "a live window is never closed");
        foreach (var holdsWindow in new[] { false, true })
            foreach (var answered in new[] { false, true })
                foreach (var responding in new[] { false, true })
                    Rule(isCli: true, holdsWindow, answered, responding, expected: false, "a command-line run is never closed");

        var pid = Environment.ProcessId;
        Check.That(!InstanceGuard.IsCliRun(pid), "no CLI mutex: not a CLI run");
        using (new Mutex(initiallyOwned: false, InstanceGuard.CliMutexName(pid)))
            Check.That(InstanceGuard.IsCliRun(pid), "a held CLI mutex marks the process as a CLI run");
        Check.That(!InstanceGuard.IsCliRun(pid), "released CLI mutex: not a CLI run");
    }

    private static void Rule(bool isCli, bool holdsWindow, bool answered, bool responding, bool expected, string what) =>
        Check.That(InstanceGuard.ShouldClose(isCli, holdsWindow, answered, responding) == expected,
            $"ShouldClose(cli={isCli}, holds={holdsWindow}, answered={answered}, responding={responding}): {what}");
}
