using CoopLauncher;

namespace LauncherTests;

/// <summary>CharacterCapture's pure rules: the newest game slot of a save folder (the system file is no slot) and the adapter's answer.</summary>
public static class CharacterCaptureTests
{
    public static void Run()
    {
        var times = new Dictionary<string, DateTime>
        {
            ["data000.bin"] = new(2026, 11, 5, 19, 31, 0), ["data001Slot.bin"] = new(2026, 11, 5, 18, 46, 0),
            ["data004Slot.bin"] = new(2026, 11, 5, 20, 7, 0), ["data00-1.bin"] = new(2026, 11, 5, 21, 0, 0), ["SS1_data000.bin"] = new(2026, 11, 6),
        };
        Check.That(CharacterCapture.NewestSlot(times.Keys, name => times[name]) == 4, "the newest game slot wins; the system file and others are no slot");
        Check.That(CharacterCapture.NewestSlot(["data000.bin"]) == 0, "data000.bin is slot 0");
        Check.That(CharacterCapture.NewestSlot(["data00-1.bin", "notes.txt"]) == null, "no game slot, none");
        Check.That(CharacterCapture.ViewOf("ok").State == CaptureState.Done, "ok is done");
        Check.That(CharacterCapture.ViewOf("failed: the save holds no character") is { State: CaptureState.Failed, Reason: "the save holds no character" },
            "a failure carries the adapter's reason");
        Check.That(CharacterCapture.ViewOf("garbage").State == CaptureState.Failed, "anything else is a failure");
    }
}
