using System.Drawing.Drawing2D;

namespace CoopLauncher.Gui;

internal enum SpinnerSize { Small, Large }

/// <summary>The in-progress mark: transitions.dev's matrix loader, orbit variant. A 4 by 4 grid of round dots with the corners
/// left out; the eight dots of the ring light up one after the other (each delayed by an eighth of the cycle) and the four
/// centre dots stay at rest. The owner runs one <see cref="Motion.Loop"/> of <see cref="Durations.Loop"/> and passes the phase
/// to every spinner it paints. With reduced motion every dot rests.</summary>
internal static class Spinner
{
    private const int Side = 4;
    private const int Gap = 2;
    private const int SmallDot = 2;
    private const int LargeDot = 3;
    private const double CycleDelayShare = 1.0 / 8;
    private const double ActiveAt = 0.15;
    private const double RestAt = 0.45;

    private static readonly int[] _ring = { 1, 2, 7, 11, 14, 13, 8, 4 };
    private static readonly IReadOnlyList<Keyframes.Stop> _pulse =
        [new(0, 0), new(ActiveAt, 1), new(RestAt, 0), new(1, 0)];

    /// <summary>The side of the square a spinner of this size takes.</summary>
    public static int BoxSize(SpinnerSize size) => Theme.Scale(Side * Dot(size) + (Side - 1) * Gap);

    /// <summary>The matrix loader's keyframes at a phase 0..1 of one dot's own cycle: 0 at rest, 1 fully lit (0 to 1 over the
    /// first 15 %, back to 0 by 45 %, each segment eased in and out).</summary>
    public static double Pulse(double ownPhase) => Keyframes.At(_pulse, ownPhase, Easing.InOut);

    /// <summary>How lit the grid's dot (0..15, row by row) is at the loop's phase: ring dot k runs its cycle k eighths late, the
    /// corners and the centre dots never light.</summary>
    public static double DotLevel(int index, double phase)
    {
        var order = Array.IndexOf(_ring, index);
        if (order < 0) return 0;
        var own = phase - order * CycleDelayShare;
        return Pulse(own - Math.Floor(own));
    }

    /// <summary>Paints the spinner centred in the box at the loop's phase.</summary>
    public static void Paint(Graphics g, Rectangle box, double phase, SpinnerSize size)
    {
        var dot = Theme.Scale(Dot(size));
        var gap = Theme.Scale(Gap);
        var span = Side * dot + (Side - 1) * gap;
        var left = box.X + (box.Width - span) / 2;
        var top = box.Y + (box.Height - span) / 2;
        var state = g.Save();
        g.SmoothingMode = SmoothingMode.AntiAlias;
        for (var index = 0; index < Side * Side; index++)
        {
            if (IsCorner(index)) continue;
            var level = Motion.Reduced ? 0 : DotLevel(index, phase);
            using var brush = new SolidBrush(Interpolate.Lerp(Theme.Raised, Theme.Text, level));
            g.FillEllipse(brush, left + index % Side * (dot + gap), top + index / Side * (dot + gap), dot, dot);
        }
        g.Restore(state);
    }

    private static int Dot(SpinnerSize size) => size == SpinnerSize.Small ? SmallDot : LargeDot;

    private static bool IsCorner(int index) => index is 0 or Side - 1 or Side * (Side - 1) or Side * Side - 1;
}
