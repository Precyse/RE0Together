namespace CoopLauncher.Gui;

/// <summary>The broadcast tool's switch (.lp-toggle) with its label: a 46x24 card track with a square thumb that slides
/// right and fills the track with the selection grey when on. Red is never used: red means live in this tool.</summary>
internal sealed class FlatToggle : Control
{
    private const string SlideChannel = "slide";
    private const string HoverChannel = "hover";

    private static readonly int TrackWidth = Theme.Scale(46);
    private static readonly int TrackHeight = Theme.Scale(24);
    private static readonly int ThumbSize = Theme.Scale(18);
    private static readonly int ThumbInset = Theme.Scale(2);
    private static readonly int TextGap = Theme.Scale(10);

    private bool _checked;
    private double _slide;
    private double _hover;

    public FlatToggle(string text)
    {
        Text = text;
        Height = Theme.ControlHeight;
        Cursor = Cursors.Hand;
        TabStop = true;
        AccessibleRole = AccessibleRole.CheckButton;
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer
                 | ControlStyles.ResizeRedraw | ControlStyles.Selectable, true);
    }

    /// <summary>Raised when the player flips the switch (not when <see cref="Checked"/> is set from code).</summary>
    public event Action<bool>? Flipped;

    public bool Checked
    {
        get => _checked;
        set
        {
            if (_checked == value) return;
            _checked = value;
            Slide();
        }
    }

    protected override AccessibleObject CreateAccessibilityInstance() => new ToggleAccessibleObject(this);

    protected override void OnMouseClick(MouseEventArgs e)
    {
        Flip();
        base.OnMouseClick(e);
    }

    protected override bool IsInputKey(Keys keyData) => keyData is Keys.Space || base.IsInputKey(keyData);

    protected override void OnKeyDown(KeyEventArgs e)
    {
        if (e.KeyCode == Keys.Space)
        {
            Flip();
            e.Handled = true;
        }
        base.OnKeyDown(e);
    }

    protected override void OnMouseEnter(EventArgs e)
    {
        Fade(1);
        base.OnMouseEnter(e);
    }

    protected override void OnMouseLeave(EventArgs e)
    {
        Fade(0);
        base.OnMouseLeave(e);
    }

    protected override void OnGotFocus(EventArgs e)
    {
        Invalidate();
        base.OnGotFocus(e);
    }

    protected override void OnLostFocus(EventArgs e)
    {
        Invalidate();
        base.OnLostFocus(e);
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;
        g.Clear(Parent?.BackColor ?? Theme.Bg);
        var track = new Rectangle(0, (Height - TrackHeight) / 2, TrackWidth, TrackHeight);
        Draw.Fill(g, Interpolate.Lerp(Theme.Card, Theme.Selection, _slide), track);
        var edge = Focused && ShowFocusCues ? Theme.Text : Interpolate.Lerp(Theme.Line, Theme.Muted, Math.Max(_slide, _hover));
        Draw.Border(g, track, edge);

        var travel = TrackWidth - 2 * ThumbInset - ThumbSize;
        var thumb = new Rectangle(track.X + ThumbInset + (int)Math.Round(travel * _slide), track.Y + (TrackHeight - ThumbSize) / 2,
            ThumbSize, ThumbSize);
        Draw.Fill(g, Theme.Text, thumb);

        var textX = track.Right + TextGap;
        Draw.Text(g, Text, Theme.Body, Theme.Text, new Rectangle(textX, 0, Width - textX, Height));
    }

    private void Flip()
    {
        _checked = !_checked;
        Slide();
        Flipped?.Invoke(_checked);
    }

    private void Slide() => Motion.Animate(this, SlideChannel, Motion.ValueOf(this, SlideChannel) ?? _slide, _checked ? 1 : 0,
        Durations.Toggle, Easing.Css, amount =>
        {
            _slide = amount;
            Invalidate();
        });

    private void Fade(double to) => Motion.Animate(this, HoverChannel, Motion.ValueOf(this, HoverChannel) ?? _hover, to,
        Durations.Hover, Easing.Css, amount =>
        {
            _hover = amount;
            Invalidate();
        });

    private sealed class ToggleAccessibleObject : ControlAccessibleObject
    {
        private readonly FlatToggle _toggle;

        public ToggleAccessibleObject(FlatToggle toggle) : base(toggle) => _toggle = toggle;

        public override AccessibleStates State => base.State | (_toggle._checked ? AccessibleStates.Checked : AccessibleStates.None);

        public override string? DefaultAction => "Toggle";

        public override void DoDefaultAction() => _toggle.Flip();
    }
}
