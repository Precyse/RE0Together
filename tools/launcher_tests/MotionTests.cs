using CoopLauncher;
using CoopLauncher.Gui;

namespace LauncherTests;

/// <summary>The motion engine's pure parts (curves, tweens, keyframes, recipes, interpolation) and the engine itself driven
/// by a fake clock and manual steps.</summary>
public static class MotionTests
{
    private const double Tolerance = 1e-6;
    private const int Samples = 100;
    private const long StartMs = 1000;

    private static readonly CubicBezier[] Curves = { Easing.Out, Easing.InOut, Easing.Css };

    public static void Run()
    {
        var clock = Motion.Clock;
        Bezier();
        Tweens();
        KeyframeValues();
        RecipeEnds();
        Interpolation();
        Engine();
        ReducedMotion();
        Retarget();
        DisposedOwner();
        Motion.Clock = clock;
        Motion.ReducedOverride = null;
    }

    private static double ReferenceEase(CubicBezier curve, double x)
    {
        double X(double t) => 3 * (1 - t) * (1 - t) * t * curve.X1 + 3 * (1 - t) * t * t * curve.X2 + t * t * t;
        double Y(double t) => 3 * (1 - t) * (1 - t) * t * curve.Y1 + 3 * (1 - t) * t * t * curve.Y2 + t * t * t;
        double low = 0, high = 1;
        for (var i = 0; i < 80; i++)
        {
            var mid = (low + high) / 2;
            if (X(mid) < x) low = mid;
            else high = mid;
        }
        return Y((low + high) / 2);
    }

    private static void Bezier()
    {
        foreach (var curve in Curves.Append(Easing.Linear))
        {
            Check.That(curve.Ease(0) == 0, $"{curve} eases 0 to 0");
            Check.That(curve.Ease(1) == 1, $"{curve} eases 1 to 1");
            Check.That(curve.Ease(-0.5) == 0 && curve.Ease(1.5) == 1, $"{curve} clamps the progress");
        }
        foreach (var curve in Curves)
        {
            var previous = 0.0;
            var monotonic = true;
            for (var i = 1; i <= Samples; i++)
            {
                var value = curve.Ease((double)i / Samples);
                monotonic &= value >= previous;
                previous = value;
            }
            Check.That(monotonic, $"{curve} never goes backwards over {Samples} samples");
        }
        var identity = Enumerable.Range(0, Samples + 1).All(i => Math.Abs(Easing.Linear.Ease((double)i / Samples) - (double)i / Samples) < Tolerance);
        Check.That(identity, "Linear is the identity");
        Check.That(Math.Abs(Easing.Out.Ease(0.5) - ReferenceEase(Easing.Out, 0.5)) < 1e-4, "Out at 0.5 matches a bisection reference");
        foreach (var curve in Curves)
            for (var i = 1; i < Samples; i++)
                if (Math.Abs(curve.Ease((double)i / Samples) - ReferenceEase(curve, (double)i / Samples)) >= 1e-4)
                    Check.That(false, $"{curve} at {i}% matches the reference");
    }

    private static void Tweens()
    {
        var tween = new Tween(10, 30, StartMs, 200, Easing.Linear);
        Check.That(tween.ValueAt(StartMs) == 10, "tween starts at From");
        Check.That(tween.ValueAt(StartMs + 100) == 20, "tween is halfway at half the time");
        Check.That(tween.ValueAt(StartMs + 200) == 30, "tween ends at To");
        Check.That(tween.ValueAt(StartMs + 5000) == 30 && tween.ValueAt(StartMs - 5000) == 10, "tween clamps outside its time");
        Check.That(!tween.DoneAt(StartMs + 199) && tween.DoneAt(StartMs + 200), "tween is done at its duration");
        Check.That(new Tween(0, 1, StartMs, 0, Easing.Out).DoneAt(StartMs), "a zero duration is done at once");

        var eased = new Tween(0, 100, StartMs, 100, Easing.Out);
        var now = StartMs + 37;
        var retargeted = new Tween(eased.ValueAt(now), 40, now, 100, Easing.Out);
        Check.That(retargeted.ValueAt(now) == eased.ValueAt(now), "a retarget from the current value does not jump");
    }

    private static void KeyframeValues()
    {
        var expected = new[] { 0.0, -1, 2, -4, 4, -4, 4, -4, 2, -1, 0 };
        for (var i = 0; i < expected.Length; i++)
            Check.That(Math.Abs(Recipes.ShakeX(i / 10.0) - expected[i]) < Tolerance, $"shake-x at {i * 10}% is {expected[i]}");

        var stops = new[] { new Keyframes.Stop(0, 0), new Keyframes.Stop(1, 10) };
        var middle = Keyframes.At(stops, 0.5, Easing.Out);
        Check.That(Math.Abs(middle - 10 * Easing.Out.Ease(0.5)) < Tolerance && Math.Abs(middle - 5) > 1, "a mid-segment value uses the curve");
        Check.That(Math.Abs(Recipes.ShakeX(0.05) - -1 * Easing.Out.Ease(0.5)) < Tolerance, "shake-x eases inside its first segment");
        Check.That(Keyframes.At(stops, -1, Easing.Out) == 0 && Keyframes.At(stops, 2, Easing.Out) == 10, "keyframes clamp the progress");
    }

    private static void RecipeEnds()
    {
        var ins = new Func<double, Layer>[] { Recipes.PanelIn, Recipes.ToastIn, Recipes.SwapIn, Recipes.DigitIn, Recipes.RevealIn,
            Recipes.DropdownOpen, Recipes.ModalOpen };
        Check.That(ins.All(recipe => recipe(1) == Layer.Rest), "every In recipe ends at rest");
        var outs = new Func<double, Layer>[] { Recipes.PanelOut, Recipes.ToastOut, Recipes.SwapOut, Recipes.RevealOut,
            Recipes.DropdownClose, Recipes.ModalClose };
        Check.That(outs.All(recipe => recipe(0) == Layer.Rest), "every Out recipe starts at rest");

        Check.That(Recipes.PanelIn(0) == new Layer(0, 0, 100, 1, 2), "PanelIn starts 100 px down, transparent, blurred 2 px");
        Check.That(Recipes.ToastIn(0) == new Layer(0, 0, 16, 0.97f, 2), "ToastIn starts 16 px down at 0.97");
        Check.That(Recipes.SwapIn(0) == new Layer(0, 0, 4, 1, 2), "SwapIn starts 4 px below");
        Check.That(Recipes.SwapOut(1) == new Layer(0, 0, -4, 1, 2), "SwapOut ends 4 px above");
        Check.That(Recipes.DigitIn(0) == new Layer(0, 0, 8, 1, 2), "DigitIn starts 8 px below");
        Check.That(Recipes.RevealIn(0) == new Layer(0, 0, 0, 1, 2) && Recipes.RevealOut(1) == new Layer(0, 0, 0, 1, 2), "reveal blurs 2 px");
        Check.That(Recipes.DropdownOpen(0) == new Layer(0, 0, 0, 0.97f, 0), "DropdownOpen starts at 0.97");
        Check.That(Recipes.DropdownClose(1) == new Layer(0, 0, 0, 0.99f, 0), "DropdownClose ends at 0.99");
        Check.That(Recipes.ModalOpen(0) == new Layer(0, 0, 0, 0.96f, 0) && Recipes.ModalClose(1) == new Layer(0, 0, 0, 0.96f, 0), "modal scales 0.96");
        Check.That(Recipes.PanelOut(1) == Recipes.PanelIn(0) && Recipes.ToastOut(1) == Recipes.ToastIn(0), "Out recipes end where In recipes start");
    }

    private static void Interpolation()
    {
        var color = Interpolate.Lerp(Color.FromArgb(0, 0, 0, 0), Color.FromArgb(255, 100, 200, 50), 0.5);
        Check.That(color == Color.FromArgb(128, 50, 100, 25), $"colours blend per channel including alpha ({color})");
        Check.That(Interpolate.Lerp(Color.Red, Color.Blue, 0).ToArgb() == Color.Red.ToArgb() && Interpolate.Lerp(Color.Red, Color.Blue, 1).ToArgb() == Color.Blue.ToArgb(), "colour ends");
        Check.That(Interpolate.Lerp(new Rectangle(0, 0, 10, 10), new Rectangle(10, 20, 30, 40), 0.5) == new Rectangle(5, 10, 20, 25), "rectangles blend");
        Check.That(Interpolate.Lerp(2.0, 4.0, 0.25) == 2.5, "numbers blend");
    }

    private static Control Window()
    {
        var control = new Control();
        _ = control.Handle;
        return control;
    }

    private static void Engine()
    {
        long now = StartMs;
        Motion.Clock = () => now;
        Motion.ReducedOverride = false;
        var owner = Window();
        var values = new List<double>();
        var done = 0;
        Motion.Animate(owner, "x", 0, 10, 100, Easing.Linear, values.Add, () => done++);
        Check.That(Motion.Active == 1 && Motion.TimerRunning, "the first animation starts the frame timer");
        Motion.Step();
        now = StartMs + 50;
        Motion.Step();
        Check.That(values.SequenceEqual(new[] { 0.0, 5.0 }) && done == 0, $"values per frame ({string.Join(",", values)})");
        Check.That(Motion.ValueOf(owner, "x") == 5, "ValueOf reads the running tween");
        now = StartMs + 100;
        Motion.Step();
        Check.That(values.SequenceEqual(new[] { 0.0, 5.0, 10.0 }) && done == 1, "the end is applied once, then done is called once");
        Check.That(Motion.Active == 0 && !Motion.TimerRunning, "no tween left: the timer is stopped");
        now = StartMs + 500;
        Motion.Step();
        Check.That(values.Count == 3 && done == 1, "a finished tween is not applied again");

        var applied = 0;
        Motion.Animate(owner, "y", 0, 1, 100, Easing.Out, _ => applied++);
        Motion.Animate(owner, "y", 0, 2, 100, Easing.Out, _ => applied++);
        Check.That(Motion.Active == 1, "a tween on the same owner and channel replaces the running one");
        Motion.Animate(owner, "z", 0, 1, 100, Easing.Out, _ => applied++);
        Motion.Finish(owner, "y");
        Check.That(Motion.Active == 1 && applied == 1, "Finish applies the end of that channel only");
        Motion.Finish(owner);
        Check.That(Motion.Active == 0 && applied == 2, "Finish without a channel ends every tween of the owner");
        Motion.Animate(owner, "w", 0, 1, 100, Easing.Out, _ => applied++);
        Motion.Cancel(owner, "w");
        Check.That(Motion.Active == 0 && applied == 2, "Cancel drops a tween without applying it");
        Motion.Step();
        Check.That(!Motion.TimerRunning, "the timer stays stopped after the last tween is gone");
        owner.Dispose();
    }

    private static void ReducedMotion()
    {
        Motion.Clock = () => StartMs;
        var owner = Window();
        Motion.ReducedOverride = true;
        var values = new List<double>();
        var done = 0;
        Motion.Animate(owner, "x", 0, 10, 400, Easing.Out, values.Add, () => done++);
        Check.That(values.SequenceEqual(new[] { 10.0 }) && done == 1 && Motion.Active == 0, "reduced motion applies the end state at once");

        Motion.ReducedOverride = false;
        Motion.Animate(owner, "x", 0, 10, 0, Easing.Out, values.Add, () => done++);
        Check.That(values.Count == 2 && done == 2 && Motion.Active == 0, "a zero duration applies at once");
        var unshown = new Control();
        Motion.Animate(unshown, "x", 0, 10, 400, Easing.Out, values.Add, () => done++);
        Check.That(values.Count == 3 && done == 3 && Motion.Active == 0, "an owner without a window applies at once");
        owner.Dispose();
    }

    private static void Retarget()
    {
        long now = StartMs;
        Motion.Clock = () => now;
        Motion.ReducedOverride = false;
        var owner = Window();
        double shown = 0;
        Motion.Animate(owner, "x", 0, 100, 100, Easing.Linear, v => shown = v);
        now = StartMs + 40;
        Motion.Step();
        var before = shown;
        Motion.Animate(owner, "x", Motion.ValueOf(owner, "x") ?? shown, 0, 100, Easing.Linear, v => shown = v);
        Motion.Step();
        Check.That(Math.Abs(before - 40) < 1e-3 && shown == before, $"retargeting from ValueOf does not jump ({before} then {shown})");
        Motion.Finish(owner);
        owner.Dispose();
    }

    private static void DisposedOwner()
    {
        long now = StartMs;
        Motion.Clock = () => now;
        Motion.ReducedOverride = false;
        var owner = Window();
        var applied = 0;
        var done = 0;
        Motion.Animate(owner, "x", 0, 1, 100, Easing.Linear, _ => applied++, () => done++);
        owner.Dispose();
        now = StartMs + 50;
        Motion.Step();
        Check.That(applied == 0 && done == 0 && Motion.Active == 0 && !Motion.TimerRunning, "a disposed owner's tween is dropped");
    }
}
