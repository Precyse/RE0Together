using CoopLauncher.Gui;

namespace LauncherTests;

/// <summary>TextChange.Classify (words swap, numbers roll) and the measuring the status line positions digit runs with.</summary>
public static class TextChangeTests
{
    private const int MeasureTolerance = 3;

    public static void Run()
    {
        Classify();
        Measure();
    }

    private static void Classify()
    {
        Check.That(TextChange.Classify("In game", "In game") == TextChange.Kind.Same, "equal texts are Same");
        Check.That(TextChange.Classify("Connected 42 ms", "Connected 57 ms") == TextChange.Kind.NumbersOnly, "a ping change is NumbersOnly");
        Check.That(TextChange.Classify("Receiving save 40%", "Receiving save 45%") == TextChange.Kind.NumbersOnly, "a percent change is NumbersOnly");
        Check.That(TextChange.Classify("Waiting for host 2:41", "Waiting for host 2:40") == TextChange.Kind.NumbersOnly, "a countdown is NumbersOnly");
        Check.That(TextChange.Classify("Connected 9 ms", "Connected 10 ms") == TextChange.Kind.NumbersOnly, "a longer number is still NumbersOnly");
        Check.That(TextChange.Classify("Connected 42 ms", "Connected") == TextChange.Kind.Words, "a vanished number is Words");
        Check.That(TextChange.Classify("Hosting", "Joined") == TextChange.Kind.Words, "other words are Words");
        Check.That(TextChange.Classify("Reconnecting 2", "Hosting") == TextChange.Kind.Words, "words and a number are Words");
        Check.That(TextChange.Classify("", "42 ms") == TextChange.Kind.Words, "text from nothing is Words");
        Check.That(TextChange.Classify("Reconnecting 2", "Waiting for host 2") == TextChange.Kind.Words, "different words around the same number are Words");
    }

    private static void Measure()
    {
        var whole = Draw.TightWidth("Connected 42 ms", Theme.Small);
        var parts = Draw.TightWidth("Connected ", Theme.Small) + Draw.TightWidth("42", Theme.Small) + Draw.TightWidth(" ms", Theme.Small);
        Check.That(Math.Abs(whole - parts) <= MeasureTolerance, $"the pieces of a line add up to the line ({whole} vs {parts})");
        Check.That(Draw.TightWidth("ab ", Theme.Small) > Draw.TightWidth("ab", Theme.Small), "trailing spaces are measured");
    }
}
