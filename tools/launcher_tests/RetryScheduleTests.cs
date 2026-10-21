using CoopLauncher;

namespace LauncherTests;

/// <summary>RetrySchedule: attempts are due once per interval from one interval after the start, and it gives up after the limit.</summary>
public static class RetryScheduleTests
{
    private const long Start = 1_000_000;
    private const long Interval = 5000;
    private const long GiveUp = 120_000;

    public static void Run()
    {
        var schedule = new RetrySchedule(Start, Interval, GiveUp);
        Check.That(!schedule.AttemptDue(Start) && !schedule.AttemptDue(Start + Interval - 1), "no attempt before one interval has passed");
        Check.That(schedule.AttemptDue(Start + Interval) && schedule.Attempts == 1, "the first attempt is due after one interval");
        Check.That(!schedule.AttemptDue(Start + Interval + 1), "not due again at once");
        Check.That(schedule.AttemptDue(Start + 2 * Interval + 700) && schedule.Attempts == 2, "the next one is due an interval after the last");
        Check.That(!schedule.AttemptDue(Start + 3 * Interval), "the interval counts from the attempt, not from the start");

        Check.That(!schedule.GaveUp(Start + GiveUp), "not given up at exactly the limit");
        Check.That(schedule.GaveUp(Start + GiveUp + 1), "given up just after the limit");

        Check.That(schedule.SecondsLeft(Start) == 120, "120 s left at the start");
        Check.That(schedule.SecondsLeft(Start + 1) == 120, "a started second still counts as left");
        Check.That(schedule.SecondsLeft(Start + 119_001) == 1, "one second left");
        Check.That(schedule.SecondsLeft(Start + GiveUp) == 0 && schedule.SecondsLeft(Start + GiveUp + 5000) == 0, "none left at and after the limit");
    }
}
