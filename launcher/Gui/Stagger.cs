namespace CoopLauncher.Gui;

/// <summary>Brings a row of controls in one after the other: each waits its place in the row times <see cref="Durations.Stagger"/>
/// hidden, then plays the recipe over itself (texts-reveal for cards, the reveal for lobby rows).</summary>
internal static class Stagger
{
    private const string DelayChannel = "stagger";

    /// <summary>Hides the control, shows it after its delay and plays the recipe over it. With reduced motion it just shows.</summary>
    public static void In(Control control, int index, int durationMs, CubicBezier curve, Func<double, Layer> recipe)
    {
        if (Motion.Reduced || control.Parent is not { IsHandleCreated: true })
        {
            control.Visible = true;
            return;
        }
        _ = control.Handle;
        control.Visible = false;
        Motion.Animate(control, DelayChannel, 0, 1, index * Durations.Stagger, Easing.Linear, _ => { }, () =>
        {
            control.Visible = true;
            MotionOverlay.Play(control, durationMs, curve, recipe, Theme.Bg);
        });
    }
}
