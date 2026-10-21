using CoopLauncher.Gui;

namespace LauncherTests;

/// <summary>The matrix loader's dot levels: the orbit ring order, each ring dot an eighth of the cycle late, the corners and the
/// centre dots never lit, the keyframes 0, 0.15, 0.45 and 1 eased in and out.</summary>
public static class SpinnerTests
{
    private const double Tolerance = 1e-6;
    private static readonly int[] Ring = { 1, 2, 7, 11, 14, 13, 8, 4 };

    public static void Run()
    {
        Pulse();
        RingOrder();
        DeadDots();
        Sizes();
    }

    private static void Pulse()
    {
        Check.That(Spinner.Pulse(0) == 0 && Spinner.Pulse(1) == 0, "a dot rests at the start and the end of its cycle");
        Check.That(Math.Abs(Spinner.Pulse(0.15) - 1) < Tolerance, "a dot is fully lit at 15 %");
        Check.That(Spinner.Pulse(0.45) == 0 && Spinner.Pulse(0.7) == 0, "a dot has rested again by 45 %");
        Check.That(Math.Abs(Spinner.Pulse(0.075) - Easing.InOut.Ease(0.5)) < Tolerance, "each segment is eased in and out");
    }

    private static void RingOrder()
    {
        for (var k = 0; k < Ring.Length; k++)
            Check.That(Math.Abs(Spinner.DotLevel(Ring[k], 0.15 + k / 8.0) - 1) < Tolerance, $"ring dot {Ring[k]} peaks {k} eighths late");
        Check.That(Math.Abs(Spinner.DotLevel(2, 0.15 + 1 / 8.0 + 1) - 1) < Tolerance, "the cycle repeats every lap");
    }

    private static void DeadDots()
    {
        foreach (var dot in new[] { 0, 3, 12, 15, 5, 6, 9, 10 })
            Check.That(Enumerable.Range(0, 20).All(i => Spinner.DotLevel(dot, i / 20.0) == 0), $"dot {dot} never lights");
    }

    private static void Sizes()
    {
        Check.That(Spinner.BoxSize(SpinnerSize.Small) == Theme.Scale(14) && Spinner.BoxSize(SpinnerSize.Large) == Theme.Scale(18), "the small spinner is 14 px and the large 18");
    }
}
