using System.Diagnostics;
using System.Runtime.InteropServices;
using Microsoft.Win32;

namespace CoopLauncher.Gui;

/// <summary>A CSS cubic-bezier easing curve: the control points (0,0) (X1,Y1) (X2,Y2) (1,1), solved for x the way WebKit's
/// UnitBezier does (Newton-Raphson, bisection when it does not converge).</summary>
public readonly record struct CubicBezier(double X1, double Y1, double X2, double Y2)
{
    private const int NewtonIterations = 8;
    private const int BisectionIterations = 64;
    private const double Epsilon = 1e-7;
    private const double MinSlope = 1e-6;

    /// <summary>The eased value (y) at the progress (x), both 0..1; progress outside the range is clamped.</summary>
    public double Ease(double progress)
    {
        if (progress <= 0) return 0;
        if (progress >= 1) return 1;
        var t = SolveForX(progress);
        return SampleY(t);
    }

    private double SampleX(double t) => ((Ax * t + Bx) * t + Cx) * t;

    private double SampleY(double t) => ((Ay * t + By) * t + Cy) * t;

    private double SlopeX(double t) => (3 * Ax * t + 2 * Bx) * t + Cx;

    private double Cx => 3 * X1;
    private double Bx => 3 * (X2 - X1) - Cx;
    private double Ax => 1 - Cx - Bx;
    private double Cy => 3 * Y1;
    private double By => 3 * (Y2 - Y1) - Cy;
    private double Ay => 1 - Cy - By;

    private double SolveForX(double x)
    {
        var t = x;
        for (var i = 0; i < NewtonIterations; i++)
        {
            var error = SampleX(t) - x;
            if (Math.Abs(error) < Epsilon) return t;
            var slope = SlopeX(t);
            if (Math.Abs(slope) < MinSlope) break;
            t -= error / slope;
        }

        double low = 0, high = 1;
        t = x;
        for (var i = 0; i < BisectionIterations; i++)
        {
            var sample = SampleX(t);
            if (Math.Abs(sample - x) < Epsilon) return t;
            if (x > sample) low = t;
            else high = t;
            t = (high - low) / 2 + low;
        }
        return t;
    }
}

/// <summary>The broadcast tool's easing curves.</summary>
public static class Easing
{
    /// <summary>base.css --ease: every panel transition unless noted.</summary>
    public static readonly CubicBezier Out = new(0.22, 1, 0.36, 1);

    /// <summary>CSS ease-in-out: skeleton-reveal and text-states-swap.</summary>
    public static readonly CubicBezier InOut = new(0.42, 0, 0.58, 1);

    /// <summary>CSS ease: the panel's plain .12s / .15s hover transitions.</summary>
    public static readonly CubicBezier Css = new(0.25, 0.1, 0.25, 1);

    /// <summary>Keyframe drivers; the curve is applied inside each keyframe segment instead.</summary>
    public static readonly CubicBezier Linear = new(0, 0, 1, 1);

    /// <summary>success-check --check-ease-bob: the check's drop settles with an overshoot.</summary>
    public static readonly CubicBezier CheckBob = new(0.34, 1.35, 0.64, 1);

    /// <summary>spinning-counter --reel-ease.</summary>
    public static readonly CubicBezier Reel = new(0.16, 1, 0.3, 1);
}

/// <summary>Every duration in milliseconds, named for the CSS token or transitions.dev recipe it comes from. Where the
/// panel tuned a recipe (springs removed, 260 ms digit roll, 280 ms shake) the panel's value is the one used.</summary>
public static class Durations
{
    /// <summary>rail.css: row hover .12s.</summary>
    public const int RailHover = 120;

    /// <summary>controls.css: button hover .15s.</summary>
    public const int Hover = 150;

    /// <summary>topbar.css: .lp-thumb transition .15s.</summary>
    public const int Toggle = 150;

    /// <summary>base.css --tabs (tabs-sliding).</summary>
    public const int Tabs = 250;

    /// <summary>card-resize.</summary>
    public const int Resize = 300;

    /// <summary>panel-reveal open.</summary>
    public const int PanelOpen = 400;

    /// <summary>panel-reveal close.</summary>
    public const int PanelClose = 350;

    /// <summary>skeleton-reveal --reveal-dur.</summary>
    public const int Reveal = 400;

    /// <summary>base.css --swap (text-states-swap).</summary>
    public const int Swap = 150;

    /// <summary>base.css --digit (feedback.css digit-roll-in).</summary>
    public const int Digit = 260;

    /// <summary>base.css --toast-open.</summary>
    public const int ToastOpen = 350;

    /// <summary>base.css --toast-close.</summary>
    public const int ToastClose = 250;

    /// <summary>base.css --shake (feedback.css shake-x).</summary>
    public const int Shake = 280;

    /// <summary>base.css --dd-open (menu-dropdown).</summary>
    public const int DropdownOpen = 250;

    /// <summary>base.css --dd-close (menu-dropdown).</summary>
    public const int DropdownClose = 150;

    /// <summary>modal open.</summary>
    public const int ModalOpen = 250;

    /// <summary>modal close.</summary>
    public const int ModalClose = 150;

    /// <summary>error-state-shake: how long the amber border is held.</summary>
    public const int ErrorHold = 3000;

    /// <summary>error-state-shake: the border fading back to the line colour.</summary>
    public const int ErrorRevert = 280;

    /// <summary>matrix-loader --matrix-cycle: one lap of every loop that borrows the loader's rhythm (spinner, comet, waiting
    /// dots, indeterminate bar).</summary>
    public const int Loop = 1200;

    /// <summary>success-check --check-opacity-dur, --check-rotate-dur, --check-bob-dur and --check-path-dur.</summary>
    public const int CheckDraw = 500;

    /// <summary>success-check --check-path-delay: the stroke starts drawing this long after the check appears.</summary>
    public const int CheckDrawDelay = 80;

    /// <summary>success-check --check-opacity-dur applied to a card's glow: the bloom up to its peak.</summary>
    public const int Bloom = 500;

    /// <summary>The bloom settling from its peak to its resting glow (panel tuning, card-resize feel).</summary>
    public const int BloomSettle = 700;

    /// <summary>The problem card's glow pulse: one lap (panel tuning, slower than the matrix loop).</summary>
    public const int ProblemPulse = 2000;

    /// <summary>error-state-shake: --shake-dur-a * 2 + --shake-dur-b * 2 (80, 80, 60, 60 ms).</summary>
    public const int ErrorShake = 280;

    /// <summary>page-side-by-side --page-slide-dur and --page-fade-dur.</summary>
    public const int Page = 250;

    /// <summary>texts-reveal --stagger-stagger: the delay between neighbours.</summary>
    public const int Stagger = 40;

    /// <summary>texts-reveal --stagger-dur: one item's reveal.</summary>
    public const int StaggerItem = 500;

    /// <summary>spinning-counter --reel-dur (the recipe's 1400 ms tuned down for a one-spin counter).</summary>
    public const int Reel = 700;

    /// <summary>spinning-counter --reel-stagger: the delay between neighbouring digit columns.</summary>
    public const int ReelStagger = 90;

    /// <summary>icon-swap --icon-swap-dur.</summary>
    public const int CopySwap = 250;

    /// <summary>How long the copy button keeps its check before it swaps back.</summary>
    public const int CopyHold = 1200;

    /// <summary>The outfit slide of the character select (page-side-by-side tuned to 250 ms).</summary>
    public const int Outfit = 250;

    /// <summary>The two cutouts crossing to each other's half in the character select.</summary>
    public const int Cross = 400;
}

/// <summary>Travel distances in 96 dpi pixels and the blur radius shared by the recipes.</summary>
public static class Distances
{
    /// <summary>panel-reveal --panel-translate-y.</summary>
    public const float PanelRise = 100;

    /// <summary>toast --toast-distance (keyframes toast-in).</summary>
    public const float ToastRise = 16;

    /// <summary>text-states-swap --text-swap-translate-y.</summary>
    public const float SwapRise = 4;

    /// <summary>feedback.css digit-in: translateY(8px).</summary>
    public const float DigitRise = 8;

    /// <summary>The 2px cross-blur of panel-reveal, skeleton-reveal, text-states-swap, toast and digit-in.</summary>
    public const float Blur = 2;

    /// <summary>page-side-by-side --page-slide-distance.</summary>
    public const float PageSlide = 8;

    /// <summary>page-side-by-side --page-blur.</summary>
    public const float PageBlur = 3;

    /// <summary>texts-reveal --stagger-distance.</summary>
    public const float StaggerRise = 12;

    /// <summary>texts-reveal --stagger-blur.</summary>
    public const float StaggerBlur = 3;

    /// <summary>success-check --check-y-amount (40 px for the recipe's 48 px icon) as the drop of the 18 px glyph.</summary>
    public const float CheckBob = 6;

    /// <summary>success-check --check-rotate-from, in degrees.</summary>
    public const float CheckRotate = 80;

    /// <summary>error-state-shake --shake-distance.</summary>
    public const float ErrorShake = 6;

    /// <summary>error-state-shake --shake-overshoot.</summary>
    public const float ErrorOvershoot = 4;

    /// <summary>The outfit slide of the character select.</summary>
    public const float OutfitSlide = 24;

    /// <summary>spinning-counter --reel-spin-blur.</summary>
    public const float ReelBlur = 3;
}

/// <summary>Start scales of the scale-in recipes.</summary>
public static class Scales
{
    /// <summary>toast --toast-scale.</summary>
    public const float Toast = 0.97f;

    /// <summary>menu-dropdown --dropdown-pre-scale.</summary>
    public const float Dropdown = 0.97f;

    /// <summary>menu-dropdown --dropdown-closing-scale.</summary>
    public const float DropdownClosing = 0.99f;

    /// <summary>modal --modal-scale.</summary>
    public const float Modal = 0.96f;
}

/// <summary>How a layer is drawn: opacity, offset in 96 dpi pixels, scale about an origin, blur radius in 96 dpi pixels.</summary>
public readonly record struct Layer(float Opacity, float OffsetX, float OffsetY, float Scale, float Blur)
{
    public static readonly Layer Rest = new(1, 0, 0, 1, 0);
}

/// <summary>Linear interpolation of the values motion drives.</summary>
public static class Interpolate
{
    public static double Lerp(double from, double to, double t) => from + (to - from) * t;

    /// <summary>Per channel including alpha, rounded and clamped to a byte.</summary>
    public static Color Lerp(Color from, Color to, double t) => Color.FromArgb(
        Channel(from.A, to.A, t), Channel(from.R, to.R, t), Channel(from.G, to.G, t), Channel(from.B, to.B, t));

    public static Rectangle Lerp(Rectangle from, Rectangle to, double t) => new(
        Round(from.X, to.X, t), Round(from.Y, to.Y, t), Round(from.Width, to.Width, t), Round(from.Height, to.Height, t));

    internal static Layer Lerp(Layer from, Layer to, double t) => new(
        (float)Lerp(from.Opacity, to.Opacity, t), (float)Lerp(from.OffsetX, to.OffsetX, t), (float)Lerp(from.OffsetY, to.OffsetY, t),
        (float)Lerp(from.Scale, to.Scale, t), (float)Lerp(from.Blur, to.Blur, t));

    private static int Channel(int from, int to, double t) => Math.Clamp(Round(from, to, t), 0, 255);

    private static int Round(int from, int to, double t) => (int)Math.Round(Lerp(from, to, t));
}

/// <summary>A CSS keyframe animation: values at progress stops with the timing function applied inside each segment.</summary>
public static class Keyframes
{
    public readonly record struct Stop(double At, double Value);

    /// <summary>The value at the progress (clamped to 0..1). Stops are in ascending order; between two stops the curve
    /// eases the local progress, as CSS animation-timing-function does per keyframe.</summary>
    public static double At(IReadOnlyList<Stop> stops, double progress, CubicBezier curve)
    {
        var p = Math.Clamp(progress, 0, 1);
        if (p <= stops[0].At) return stops[0].Value;
        for (var i = 1; i < stops.Count; i++)
        {
            if (p > stops[i].At) continue;
            var from = stops[i - 1];
            var to = stops[i];
            return Interpolate.Lerp(from.Value, to.Value, curve.Ease((p - from.At) / (to.At - from.At)));
        }
        return stops[^1].Value;
    }
}

/// <summary>The recipes as pure functions of the eased progress. "In" recipes run from their hidden state (0) to
/// <see cref="Layer.Rest"/> (1); "Out" recipes from rest (0) to their hidden state (1).</summary>
public static class Recipes
{
    private const double ShakeStep = 0.1;
    private const double ErrorShakeLegA = 80.0 / Durations.ErrorShake;
    private const double ErrorShakeRightAt = ErrorShakeLegA;
    private const double ErrorShakeLeftAt = 2 * ErrorShakeLegA;
    private const double ErrorShakeBackAt = 2 * ErrorShakeLegA + 60.0 / Durations.ErrorShake;
    private static readonly double[] _shakePixels = { 0, -1, 2, -4, 4, -4, 4, -4, 2, -1, 0 };
    private static readonly IReadOnlyList<Keyframes.Stop> _shakeStops = _shakePixels
        .Select((pixels, i) => new Keyframes.Stop(i * ShakeStep, pixels)).ToArray();

    private static readonly Layer _panelHidden = new(0, 0, Distances.PanelRise, 1, Distances.Blur);
    private static readonly Layer _toastHidden = new(0, 0, Distances.ToastRise, Scales.Toast, Distances.Blur);
    private static readonly Layer _swapAbove = new(0, 0, -Distances.SwapRise, 1, Distances.Blur);
    private static readonly Layer _swapBelow = new(0, 0, Distances.SwapRise, 1, Distances.Blur);
    private static readonly Layer _digitHidden = new(0, 0, Distances.DigitRise, 1, Distances.Blur);
    private static readonly Layer _revealHidden = new(0, 0, 0, 1, Distances.Blur);
    private static readonly Layer _dropdownHidden = new(0, 0, 0, Scales.Dropdown, 0);
    private static readonly Layer _dropdownClosed = new(0, 0, 0, Scales.DropdownClosing, 0);
    private static readonly Layer _modalHidden = new(0, 0, 0, Scales.Modal, 0);
    private static readonly Layer _staggerHidden = new(0, 0, Distances.StaggerRise, 1, Distances.StaggerBlur);
    private static readonly IReadOnlyList<Keyframes.Stop> _errorShakeStops =
    [
        new(0, 0), new(ErrorShakeRightAt, Distances.ErrorShake), new(ErrorShakeLeftAt, -Distances.ErrorShake),
        new(ErrorShakeBackAt, Distances.ErrorOvershoot), new(1, 0),
    ];

    public static Layer PanelIn(double p) => Interpolate.Lerp(_panelHidden, Layer.Rest, p);
    public static Layer PanelOut(double p) => Interpolate.Lerp(Layer.Rest, _panelHidden, p);
    public static Layer ToastIn(double p) => Interpolate.Lerp(_toastHidden, Layer.Rest, p);
    public static Layer ToastOut(double p) => Interpolate.Lerp(Layer.Rest, _toastHidden, p);
    public static Layer SwapOut(double p) => Interpolate.Lerp(Layer.Rest, _swapAbove, p);
    public static Layer SwapIn(double p) => Interpolate.Lerp(_swapBelow, Layer.Rest, p);
    public static Layer DigitIn(double p) => Interpolate.Lerp(_digitHidden, Layer.Rest, p);
    public static Layer RevealIn(double p) => Interpolate.Lerp(_revealHidden, Layer.Rest, p);
    public static Layer RevealOut(double p) => Interpolate.Lerp(Layer.Rest, _revealHidden, p);
    public static Layer DropdownOpen(double p) => Interpolate.Lerp(_dropdownHidden, Layer.Rest, p);
    public static Layer DropdownClose(double p) => Interpolate.Lerp(Layer.Rest, _dropdownClosed, p);
    public static Layer ModalOpen(double p) => Interpolate.Lerp(_modalHidden, Layer.Rest, p);
    public static Layer ModalClose(double p) => Interpolate.Lerp(Layer.Rest, _modalHidden, p);
    public static Layer StaggerIn(double p) => Interpolate.Lerp(_staggerHidden, Layer.Rest, p);

    /// <summary>page-side-by-side: the page that arrives. Direction 1 comes in from the right (forward), -1 from the left.</summary>
    public static Layer PageIn(double p, int direction) => Slide(Distances.PageSlide, Distances.PageBlur, direction, entering: true, p);

    /// <summary>page-side-by-side: the page that leaves, to the left for direction 1, to the right for -1.</summary>
    public static Layer PageOut(double p, int direction) => Slide(Distances.PageSlide, Distances.PageBlur, direction, entering: false, p);

    /// <summary>The outfit of the character select arriving: from the right for direction 1 (next), from the left for -1.</summary>
    public static Layer OutfitIn(double p, int direction) => Slide(Distances.OutfitSlide, Distances.PageBlur, direction, entering: true, p);

    /// <summary>The outfit leaving: to the left for direction 1 (next), to the right for -1.</summary>
    public static Layer OutfitOut(double p, int direction) => Slide(Distances.OutfitSlide, Distances.PageBlur, direction, entering: false, p);

    private static Layer Slide(float distance, float blur, int direction, bool entering, double p) => entering
        ? Interpolate.Lerp(new Layer(0, direction * distance, 0, 1, blur), Layer.Rest, p)
        : Interpolate.Lerp(Layer.Rest, new Layer(0, -direction * distance, 0, 1, blur), p);

    /// <summary>error-state-shake: the horizontal offset in 96 dpi pixels at the linear progress of <see cref="Durations.ErrorShake"/>
    /// (0, +6, -6, +4, 0 at 0, 28.57, 57.14, 78.57, 100 %), each leg eased with <see cref="Easing.Out"/>.</summary>
    public static double ErrorShakeX(double linearProgress) => Keyframes.At(_errorShakeStops, linearProgress, Easing.Out);

    /// <summary>success-check: the glyph's drop in 96 dpi pixels at the linear progress of <see cref="Durations.CheckDraw"/>
    /// (from <see cref="Distances.CheckBob"/> to rest, overshooting a little on <see cref="Easing.CheckBob"/>).</summary>
    public static double CheckBob(double linearProgress) => Distances.CheckBob * (1 - Easing.CheckBob.Ease(Math.Clamp(linearProgress, 0, 1)));

    /// <summary>feedback.css shake-x: the horizontal offset in 96 dpi pixels at the linear progress, each keyframe
    /// segment eased with <see cref="Easing.Out"/> (the class's animation-timing-function).</summary>
    public static double ShakeX(double linearProgress) => Keyframes.At(_shakeStops, linearProgress, Easing.Out);
}

/// <summary>A value moving from one number to another over a duration on a curve.</summary>
public readonly record struct Tween(double From, double To, long StartMs, int DurationMs, CubicBezier Curve)
{
    /// <summary>Linear progress 0..1 at the time; a zero duration is finished at its start.</summary>
    public double Progress(long nowMs) => DurationMs <= 0 ? 1 : Math.Clamp((double)(nowMs - StartMs) / DurationMs, 0, 1);

    public double ValueAt(long nowMs) => Interpolate.Lerp(From, To, Curve.Ease(Progress(nowMs)));

    public bool DoneAt(long nowMs) => nowMs - StartMs >= DurationMs;
}

/// <summary>The animation engine (UI thread only): one frame pacer locked to the display's refresh (FramePacer) drives every tween and
/// stops when none is left. Tweens are keyed by owner and channel; starting one replaces the running one. Nothing here
/// blocks input.</summary>
public static class Motion
{
    private const uint GetClientAreaAnimation = 0x1042;
    private const int MillisecondsPerSecond = 1000;

    private sealed class Running
    {
        public required Control Owner { get; init; }
        public required string Channel { get; init; }
        public required Tween Tween { get; init; }
        public required Action<double> Apply { get; init; }
        public Action? Done { get; init; }
        public bool Removed { get; set; }
    }

    private sealed class LoopRun
    {
        public required Control Owner { get; init; }
        public required string Channel { get; init; }
        public required int PeriodMs { get; init; }
        public required Action<double> Apply { get; init; }
        public required long StartMs { get; init; }
        public bool Removed { get; set; }
    }

    private static readonly List<Running> _running = new();
    private static readonly List<LoopRun> _loops = new();
    private static readonly HashSet<Control> _watched = new();
    private static readonly HashSet<Form> _watchedForms = new();
    private static readonly long _ticksPerSecond = Stopwatch.Frequency;
    private static FramePacer? _pacer;
    private static volatile bool _systemStale = true;
    private static bool _systemReduced;

    static Motion()
    {
        SystemEvents.UserPreferenceChanged += (_, _) => _systemStale = true;
    }

    /// <summary>Milliseconds on a monotonic clock; frame shots and tests replace it.</summary>
    public static Func<long> Clock { get; set; } = () => Stopwatch.GetTimestamp() * MillisecondsPerSecond / _ticksPerSecond;

    /// <summary>Forces reduced motion on or off (tests, shots); null follows Windows.</summary>
    public static bool? ReducedOverride { get; set; }

    /// <summary>With Windows' "Animation effects" off every animation applies its end state at once.</summary>
    public static bool Reduced => ReducedOverride ?? SystemReduced();

    /// <summary>The tweens still running.</summary>
    public static int Active => _running.Count;

    /// <summary>The loops registered and not cancelled, shown or not.</summary>
    public static int Loops => _loops.Count;

    internal static bool TimerRunning => _pacer is { Running: true };

    /// <summary>Moves a value from one number to another and reports it every frame, then once with <paramref name="to"/>
    /// before <paramref name="done"/>. A tween on the same owner and channel is replaced (callers start from
    /// <see cref="ValueOf"/> so a retarget never jumps). Reduced motion, an owner without a window and a zero duration
    /// apply the end state and finish at once.</summary>
    public static void Animate(Control owner, string channel, double from, double to, int durationMs, CubicBezier curve,
        Action<double> apply, Action? done = null)
    {
        Remove(Find(owner, channel));
        if (Reduced || durationMs <= 0 || owner.IsDisposed || !owner.IsHandleCreated)
        {
            apply(to);
            done?.Invoke();
            return;
        }

        _running.Add(new Running
        {
            Owner = owner, Channel = channel, Apply = apply, Done = done,
            Tween = new Tween(from, to, Clock(), durationMs, curve),
        });
        StartTimer();
    }

    /// <summary>Reports a phase 0..1 every frame until <see cref="Cancel"/> or the owner is disposed, one lap per period (a
    /// loop on the same owner and channel is replaced). The first phase is 0 at once. A loop whose owner is not visible, or
    /// whose form is minimized, neither applies nor keeps the frame timer running; it carries on from where the clock is when
    /// the owner shows again. Reduced motion applies phase 0 once and registers nothing.</summary>
    public static void Loop(Control owner, string channel, int periodMs, Action<double> apply)
    {
        Cancel(owner, channel);
        if (owner.IsDisposed) return;
        if (Reduced)
        {
            apply(0);
            return;
        }

        var run = new LoopRun { Owner = owner, Channel = channel, PeriodMs = periodMs, Apply = apply, StartMs = Clock() };
        _loops.Add(run);
        Watch(owner);
        if (!Shows(owner)) return;
        apply(0);
        StartTimer();
    }

    /// <summary>The phase 0..1 of a loop that started at <paramref name="startMs"/>, <paramref name="periodMs"/> per lap.</summary>
    public static double LoopPhase(long startMs, long nowMs, int periodMs)
    {
        if (periodMs <= 0) return 0;
        var into = (nowMs - startMs) % periodMs;
        if (into < 0) into += periodMs;
        return (double)into / periodMs;
    }

    /// <summary>The current value of the running tween, or null when there is none.</summary>
    public static double? ValueOf(Control owner, string channel) => Find(owner, channel)?.Tween.ValueAt(Clock());

    /// <summary>Drops the owner's tween or loop on the channel without applying anything.</summary>
    public static void Cancel(Control owner, string channel)
    {
        Remove(Find(owner, channel));
        Remove(_loops.FirstOrDefault(l => l.Owner == owner && l.Channel == channel));
    }

    /// <summary>Jumps the owner's tween on the channel (every channel when null) to its end.</summary>
    public static void Finish(Control owner, string? channel = null)
    {
        foreach (var run in _running.Where(r => r.Owner == owner && (channel == null || r.Channel == channel)).ToArray())
        {
            Remove(run);
            run.Apply(run.Tween.To);
            run.Done?.Invoke();
        }
    }

    /// <summary>One frame: applies every running tween, finishes the ones that ended, drops the ones whose owner is gone,
    /// applies every loop whose owner shows, and stops the timer when no tween and no shown loop is left.</summary>
    public static void Step()
    {
        var now = Clock();
        StepLoops(now);
        foreach (var run in _running.ToArray())
        {
            if (run.Removed) continue;
            if (run.Owner.IsDisposed)
            {
                Remove(run);
            }
            else if (run.Tween.DoneAt(now))
            {
                Remove(run);
                run.Apply(run.Tween.To);
                run.Done?.Invoke();
            }
            else
            {
                run.Apply(run.Tween.ValueAt(now));
            }
        }
        if (_running.Count == 0 && !_loops.Any(l => Shows(l.Owner))) StopTimer();
    }

    private static void StepLoops(long now)
    {
        foreach (var run in _loops.ToArray())
        {
            if (run.Removed) continue;
            if (run.Owner.IsDisposed) Remove(run);
            else if (Shows(run.Owner)) run.Apply(LoopPhase(run.StartMs, now, run.PeriodMs));
        }
    }

    /// <summary>A loop paints only for an owner that is visible in a form that is not minimized.</summary>
    private static bool Shows(Control owner) => owner.Visible && owner.FindForm() is not { WindowState: FormWindowState.Minimized };

    /// <summary>Watches what wakes a loop up: its owner showing again, and its form being restored.</summary>
    private static void Watch(Control owner)
    {
        if (_watched.Add(owner))
        {
            owner.VisibleChanged += (_, _) => Wake();
            owner.ParentChanged += (_, _) => WatchForm(owner);
            owner.Disposed += (_, _) => _watched.Remove(owner);
        }
        WatchForm(owner);
    }

    private static void WatchForm(Control owner)
    {
        if (owner.FindForm() is not { } form || !_watchedForms.Add(form)) return;
        form.Resize += (_, _) => Wake();
        form.Disposed += (_, _) => _watchedForms.Remove(form);
    }

    private static void Wake()
    {
        if (_loops.Any(l => Shows(l.Owner))) StartTimer();
    }

    private static Running? Find(Control owner, string channel) =>
        _running.FirstOrDefault(r => r.Owner == owner && r.Channel == channel);

    private static void Remove(Running? run)
    {
        if (run == null) return;
        run.Removed = true;
        _running.Remove(run);
    }

    private static void Remove(LoopRun? run)
    {
        if (run == null) return;
        run.Removed = true;
        _loops.Remove(run);
    }

    private static void StartTimer()
    {
        _pacer ??= new FramePacer(SynchronizationContext.Current, Step);
        _pacer.Start();
    }

    private static void StopTimer() => _pacer?.Stop();

    private static bool SystemReduced()
    {
        if (!_systemStale) return _systemReduced;
        _systemStale = false;
        var enabled = 1;
        _systemReduced = SystemParametersInfo(GetClientAreaAnimation, 0, ref enabled, 0) && enabled == 0;
        return _systemReduced;
    }

    [DllImport("user32.dll")]
    private static extern bool SystemParametersInfo(uint action, uint param, ref int value, uint winIni);
}
