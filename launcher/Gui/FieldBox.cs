namespace CoopLauncher.Gui;

/// <summary>A one-line text field: dark well, 1px line border that turns white while focused. <see cref="Refuse"/> marks
/// the entered text as refused: the border turns amber, the field shakes, and the amber is held and then fades back
/// (error-state-shake with the panel's own shake-x); typing clears it at once.</summary>
internal sealed class FieldBox : Panel
{
    private const string ShakeChannel = "shake";
    private const string RevertChannel = "revert";

    private static readonly int InsetX = Theme.Scale(10);
    private static readonly int InsetY = Theme.Scale(9);

    private readonly System.Windows.Forms.Timer _hold = new() { Interval = Durations.ErrorHold };
    private int _shakeX;
    private double _refused;

    public TextBox Input { get; } = new()
    {
        BorderStyle = BorderStyle.None,
        BackColor = Theme.Bg,
        ForeColor = Theme.Text,
        Font = Theme.Mono,
        Dock = DockStyle.Fill,
    };

    public FieldBox()
    {
        Height = Theme.ControlHeight;
        BackColor = Theme.Bg;
        Padding = new Padding(InsetX, InsetY, InsetX, 0);
        Controls.Add(Input);
        Input.GotFocus += (_, _) => Invalidate();
        Input.LostFocus += (_, _) => Invalidate();
        Input.TextChanged += (_, _) => Clear();
        _hold.Tick += (_, _) => Revert();
        SetStyle(ControlStyles.ResizeRedraw, true);
    }

    /// <summary>Marks the text as refused.</summary>
    public void Refuse()
    {
        _hold.Stop();
        Motion.Cancel(this, RevertChannel);
        _refused = 1;
        Motion.Animate(this, ShakeChannel, 0, 1, Durations.Shake, Easing.Linear, progress =>
        {
            Shake((int)Math.Round(Theme.ScaleF((float)Recipes.ShakeX(progress))));
        });
        _hold.Start();
        Invalidate();
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing) _hold.Dispose();
        base.Dispose(disposing);
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        base.OnPaint(e);
        var baseColor = Input.Focused ? Theme.Text : Theme.Line;
        var border = new Rectangle(_shakeX, 0, Width, Height);
        Draw.Border(e.Graphics, border, Interpolate.Lerp(baseColor, Theme.Problem, _refused));
    }

    private void Shake(int offset)
    {
        if (_shakeX == offset) return;
        _shakeX = offset;
        Padding = new Padding(InsetX + offset, InsetY, InsetX - offset, 0);
        Invalidate();
    }

    private void Revert()
    {
        _hold.Stop();
        Motion.Animate(this, RevertChannel, _refused, 0, Durations.ErrorRevert, Easing.Css, amount =>
        {
            _refused = amount;
            Invalidate();
        });
    }

    private void Clear()
    {
        _hold.Stop();
        Motion.Cancel(this, RevertChannel);
        Motion.Cancel(this, ShakeChannel);
        Shake(0);
        if (_refused == 0) return;
        _refused = 0;
        Invalidate();
    }
}
