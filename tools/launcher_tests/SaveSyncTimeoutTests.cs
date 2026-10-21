using CoopLauncher;

namespace LauncherTests;

/// <summary>SaveSyncTimeout: the guest waits 60 s for the host's first frame, then 30 s between frames.</summary>
public static class SaveSyncTimeoutTests
{
    private const long Start = 1_000;
    private const long FirstFrameAfterMs = 50_000;
    private const long LastFrameAfterMs = 10_000;

    public static void Run()
    {
        var neverAnswered = Start + SaveSyncTimeout.NoAnswerTimeoutMs;
        Check.That(!SaveSyncTimeout.TimedOut(Start, null, 0, ready: true, neverAnswered * 10), "a complete set never times out");
        Check.That(!SaveSyncTimeout.TimedOut(Start, null, 0, ready: false, neverAnswered), "no frame yet: the limit itself is still waiting");
        Check.That(SaveSyncTimeout.TimedOut(Start, null, 0, ready: false, neverAnswered + 1), "no frame within 60 s: timed out");

        var first = Start + FirstFrameAfterMs;
        var last = first + LastFrameAfterMs;
        var stalled = last + SaveSyncTimeout.StallTimeoutMs;
        Check.That(!SaveSyncTimeout.TimedOut(Start, first, last, ready: false, stalled), "frames arrived: the stall limit itself is still waiting");
        Check.That(SaveSyncTimeout.TimedOut(Start, first, last, ready: false, stalled + 1), "no new frame for 30 s: timed out");
        Check.That(!SaveSyncTimeout.TimedOut(Start, first, last, ready: true, stalled + 1), "a complete set does not stall");
        Check.That(!SaveSyncTimeout.TimedOut(Start, Start + 1, neverAnswered + 5_000, ready: false, neverAnswered + 6_000), "the 60 s start limit no longer applies once frames arrived");
    }
}
