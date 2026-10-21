namespace CoopLauncher.Gui;

internal enum ButtonKind { Normal, Primary, Ghost, Icon }

/// <summary>A flat, squared button: Primary is the one white action, Normal a raised card, Ghost an outline and Icon a bare
/// glyph (Theme.Icon), both transparent over whatever is behind them, on a card fill while hovered (Icon: or active). The fill and
/// ink fade to their hover colours (controls.css .15s); a press snaps to a darker fill. It takes keyboard focus (Tab),
/// clicks on Space and Enter, and is a dialog button (default button while focused).</summary>
internal sealed class FlatButton : Control, IButtonControl
{
    private const string HoverChannel = "hover";
    private const double PressDarken = 0.35;
    private const int IconFillAlpha = 220;

    /// <summary>The rest fill of a ghost: the selection grey at no alpha, so the hover fades the alpha only.</summary>
    private static readonly Color Clear = Color.FromArgb(0, Theme.Selection);

    private static readonly int ArrowGap = Theme.Scale(8);

    private ButtonKind _kind;
    private bool _hovered;
    private bool _pressed;
    private double _hover;
    private bool _active;

    public FlatButton(string text, ButtonKind kind)
    {
        Text = kind == ButtonKind.Primary ? text.ToUpperInvariant() : text;
        Height = Theme.ControlHeight;
        Cursor = Cursors.Hand;
        TabStop = true;
        AccessibleRole = AccessibleRole.PushButton;
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer
                 | ControlStyles.ResizeRedraw | ControlStyles.Selectable | ControlStyles.StandardClick
                 | ControlStyles.SupportsTransparentBackColor, true);
        Kind = kind;
    }

    /// <summary>The look of the button; it can change while the button lives (a friend's game becoming the selected one).</summary>
    public ButtonKind Kind
    {
        get => _kind;
        set
        {
            _kind = value;
            BackColor = value is ButtonKind.Icon or ButtonKind.Ghost ? Color.Transparent : Theme.Bg;
            Invalidate();
        }
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

    /// <summary>An arrow drawn after the label, or none. A button without a label draws its arrows next to each other.</summary>
    public ArrowDirection? ArrowAfter { get; set; }

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
        if (_kind != ButtonKind.Ghost) e.Graphics.Clear(Parent?.BackColor ?? Theme.Bg);
        Draw.Fill(e.Graphics, fill, ClientRectangle);
        if (outline is { } line) Draw.Border(e.Graphics, ClientRectangle, line);
        var font = _kind == ButtonKind.Primary ? Theme.Primary : Theme.Strong;
        PaintContent(e.Graphics, font, ink);
        if (Focused && ShowFocusCues) Draw.Border(e.Graphics, Rectangle.Inflate(ClientRectangle, -1, -1), Theme.Text);
    }

    /// <summary>The label with its arrows, together centred in the button.</summary>
    private void PaintContent(Graphics g, Font font, Color ink)
    {
        if (Arrow == null && ArrowAfter == null)
        {
            Draw.Centered(g, Text, font, ink, ClientRectangle);
            return;
        }
        var textWidth = Text.Length > 0 ? Draw.Width(Text, font) : 0;
        var parts = new[] { Arrow != null ? Theme.ArrowSize : 0, textWidth, ArrowAfter != null ? Theme.ArrowSize : 0 };
        var x = (Width - parts.Sum() - ArrowGap * (parts.Count(width => width > 0) - 1)) / 2;
        if (Arrow is { } before)
        {
            Draw.Arrow(g, ink, new Point(x + Theme.ArrowSize / 2, Height / 2), before);
            x += Theme.ArrowSize + ArrowGap;
        }
        if (textWidth > 0)
        {
            Draw.Text(g, Text, font, ink, new Rectangle(x, 0, textWidth, Height));
            x += textWidth + ArrowGap;
        }
        if (ArrowAfter is { } after) Draw.Arrow(g, ink, new Point(x + Theme.ArrowSize / 2, Height / 2), after);
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
        if (!Enabled) return _kind == ButtonKind.Primary ? (Theme.Selection, Theme.Muted, null) : (Clear, Theme.Dim, Theme.Line);
        var (restFill, restInk, outline) = Palette(hovered: false);
        var (hoverFill, hoverInk, _) = Palette(hovered: true);
        var fill = Interpolate.Lerp(restFill, hoverFill, _hover);
        if (_pressed) fill = Interpolate.Lerp(hoverFill, Theme.Bg, PressDarken);
        return (fill, Interpolate.Lerp(restInk, hoverInk, _hover), outline);
    }

    private (Color Fill, Color Ink, Color? Outline) Palette(bool hovered) => _kind switch
    {
        ButtonKind.Primary => hovered ? (Theme.Line, Theme.Text, null) : (Theme.Text, Theme.Bg, null),
        ButtonKind.Ghost => (hovered ? Theme.Selection : Clear, Theme.Text, Theme.Line),
        _ => (hovered ? Theme.CardHover : Theme.Card, Theme.Text, null),
    };
}
