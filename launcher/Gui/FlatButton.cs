namespace CoopLauncher.Gui;

internal enum ButtonKind { Normal, Primary, Ghost, Icon }

/// <summary>A flat, squared button: Primary is the one white action, Normal a raised card, Ghost an outline, Icon a bare
/// glyph (Theme.Icon) over whatever is behind it, on a card fill while hovered or active. The fill and
/// ink fade to their hover colours (controls.css .15s); a press snaps to a darker fill. It takes keyboard focus (Tab),
/// clicks on Space and Enter, and is a dialog button (default button while focused).</summary>
internal sealed class FlatButton : Control, IButtonControl
{
    private const string HoverChannel = "hover";
    private const double PressDarken = 0.35;
    private const int IconFillAlpha = 220;

    private static readonly int ArrowGap = Theme.Scale(8);

    private readonly ButtonKind _kind;
    private bool _hovered;
    private bool _pressed;
    private double _hover;
    private bool _active;

    public FlatButton(string text, ButtonKind kind)
    {
        _kind = kind;
        Text = kind == ButtonKind.Primary ? text.ToUpperInvariant() : text;
        Height = Theme.ControlHeight;
        Cursor = Cursors.Hand;
        TabStop = true;
        AccessibleRole = AccessibleRole.PushButton;
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer
                 | ControlStyles.ResizeRedraw | ControlStyles.Selectable | ControlStyles.StandardClick, true);
        if (kind != ButtonKind.Icon) return;
        SetStyle(ControlStyles.SupportsTransparentBackColor, true);
        BackColor = Color.Transparent;
    }

    /// <summary>An Icon button whose panel is open reads at full ink.</summary>
    public bool Active
    {
        get => _active;
        set
        {
            if (_active == value) return;
            _active = value;
            Invalidate();
        }
    }

    /// <summary>An arrow drawn before the label, or none.</summary>
    public ArrowDirection? Arrow { get; set; }

    public DialogResult DialogResult { get; set; }

    public void NotifyDefault(bool value)
    {
    }

    public void PerformClick()
    {
        if (Enabled && Visible) OnClick(EventArgs.Empty);
    }

    protected override void OnClick(EventArgs e)
    {
        if (DialogResult != DialogResult.None && FindForm() is { } form) form.DialogResult = DialogResult;
        base.OnClick(e);
    }

    protected override void OnTextChanged(EventArgs e)
    {
        AccessibleName = Text;
        Invalidate();
        base.OnTextChanged(e);
    }

    protected override void OnMouseEnter(EventArgs e)
    {
        SetHovered(true);
        base.OnMouseEnter(e);
    }

    protected override void OnMouseLeave(EventArgs e)
    {
        SetHovered(false);
        base.OnMouseLeave(e);
    }

    protected override void OnMouseDown(MouseEventArgs e)
    {
        if (e.Button == MouseButtons.Left) SetPressed(true);
        base.OnMouseDown(e);
    }

    protected override void OnMouseUp(MouseEventArgs e)
    {
        SetPressed(false);
        base.OnMouseUp(e);
    }

    protected override bool IsInputKey(Keys keyData) => keyData is Keys.Space || base.IsInputKey(keyData);

    protected override void OnKeyDown(KeyEventArgs e)
    {
        if (e.KeyCode == Keys.Space)
        {
            SetPressed(true);
            e.Handled = true;
        }
        base.OnKeyDown(e);
    }

    protected override void OnKeyUp(KeyEventArgs e)
    {
        if (e.KeyCode == Keys.Space && _pressed)
        {
            SetPressed(false);
            PerformClick();
            e.Handled = true;
        }
        base.OnKeyUp(e);
    }

    protected override void OnGotFocus(EventArgs e)
    {
        Invalidate();
        base.OnGotFocus(e);
    }

    protected override void OnLostFocus(EventArgs e)
    {
        SetPressed(false);
        Invalidate();
        base.OnLostFocus(e);
    }

    protected override void OnEnabledChanged(EventArgs e)
    {
        Cursor = Enabled ? Cursors.Hand : Cursors.Default;
        _hovered = false;
        _pressed = false;
        Motion.Cancel(this, HoverChannel);
        _hover = 0;
        Invalidate();
        base.OnEnabledChanged(e);
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        if (_kind == ButtonKind.Icon)
        {
            PaintIcon(e.Graphics);
            return;
        }
        var (fill, ink, outline) = Colors();
        e.Graphics.Clear(Parent?.BackColor ?? Theme.Bg);
        Draw.Fill(e.Graphics, fill, ClientRectangle);
        if (outline is { } line) Draw.Border(e.Graphics, ClientRectangle, line);
        var font = _kind == ButtonKind.Primary ? Theme.Primary : Theme.Strong;
        if (Arrow is not { } arrow)
        {
            Draw.Centered(e.Graphics, Text, font, ink, ClientRectangle);
        }
        else
        {
            var textWidth = Draw.Width(Text, font);
            var left = (Width - Theme.ArrowSize - ArrowGap - textWidth) / 2;
            Draw.Arrow(e.Graphics, ink, new Point(left + Theme.ArrowSize / 2, Height / 2), arrow);
            Draw.Text(e.Graphics, Text, font, ink, new Rectangle(left + Theme.ArrowSize + ArrowGap, 0, textWidth, Height));
        }
        if (Focused && ShowFocusCues) Draw.Border(e.Graphics, Rectangle.Inflate(ClientRectangle, -1, -1), Theme.Text);
    }

    private void PaintIcon(Graphics g)
    {
        var lit = _active ? 1 : _hover;
        if (Enabled && lit > 0)
        {
            using var fill = new SolidBrush(Color.FromArgb((int)(IconFillAlpha * lit), Theme.Card));
            g.FillRectangle(fill, ClientRectangle);
        }
        Draw.Centered(g, Text, Theme.Icon, Enabled ? Theme.Text : Theme.Dim, ClientRectangle);
        if (Focused && ShowFocusCues) Draw.Border(g, Rectangle.Inflate(ClientRectangle, -1, -1), Theme.Text);
    }

    private void SetHovered(bool hovered)
    {
        if (_hovered == hovered || !Enabled) return;
        _hovered = hovered;
        Motion.Animate(this, HoverChannel, Motion.ValueOf(this, HoverChannel) ?? _hover, hovered ? 1 : 0, Durations.Hover, Easing.Css,
            amount =>
            {
                _hover = amount;
                Invalidate();
            });
    }

    private void SetPressed(bool pressed)
    {
        if (_pressed == pressed) return;
        _pressed = pressed;
        Invalidate();
    }

    private (Color Fill, Color Ink, Color? Outline) Colors()
    {
        var parent = Parent?.BackColor ?? Theme.Bg;
        if (!Enabled) return (parent, Theme.Dim, Theme.Line);
        var (restFill, restInk, outline) = Palette(parent, hovered: false);
        var (hoverFill, hoverInk, _) = Palette(parent, hovered: true);
        var fill = Interpolate.Lerp(restFill, hoverFill, _hover);
        if (_pressed) fill = Interpolate.Lerp(hoverFill, Theme.Bg, PressDarken);
        return (fill, Interpolate.Lerp(restInk, hoverInk, _hover), outline);
    }

    private (Color Fill, Color Ink, Color? Outline) Palette(Color parent, bool hovered) => _kind switch
    {
        ButtonKind.Primary => hovered ? (Theme.Line, Theme.Text, null) : (Theme.Text, Theme.Bg, null),
        ButtonKind.Ghost => (hovered ? Theme.Card : parent, Theme.Text, Theme.Line),
        _ => (hovered ? Theme.CardHover : Theme.Card, Theme.Text, null),
    };
}
