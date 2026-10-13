namespace CoopLauncher.Gui;

internal enum ButtonKind { Normal, Primary, Ghost }

/// <summary>A flat, squared button: Primary is the one white action, Normal a raised card, Ghost an outline.</summary>
internal sealed class FlatButton : Control
{
    private static readonly int ArrowGap = Theme.Scale(8);

    private readonly ButtonKind _kind;
    private bool _hover;

    public FlatButton(string text, ButtonKind kind)
    {
        _kind = kind;
        Text = kind == ButtonKind.Primary ? text.ToUpperInvariant() : text;
        Height = Theme.ControlHeight;
        Cursor = Cursors.Hand;
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer
                 | ControlStyles.ResizeRedraw, true);
    }

    /// <summary>An arrow drawn before the label, or none.</summary>
    public ArrowDirection? Arrow { get; set; }

    protected override void OnMouseEnter(EventArgs e)
    {
        _hover = true;
        Invalidate();
        base.OnMouseEnter(e);
    }

    protected override void OnMouseLeave(EventArgs e)
    {
        _hover = false;
        Invalidate();
        base.OnMouseLeave(e);
    }

    protected override void OnEnabledChanged(EventArgs e)
    {
        Cursor = Enabled ? Cursors.Hand : Cursors.Default;
        Invalidate();
        base.OnEnabledChanged(e);
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        var (fill, ink, outline) = Colors();
        e.Graphics.Clear(Parent?.BackColor ?? Theme.Bg);
        using (var brush = new SolidBrush(fill)) e.Graphics.FillRectangle(brush, ClientRectangle);
        if (outline is { } line) Draw.Border(e.Graphics, ClientRectangle, line);
        var font = _kind == ButtonKind.Primary ? Theme.Primary : Theme.Strong;
        if (Arrow is not { } arrow)
        {
            Draw.Centered(e.Graphics, Text, font, ink, ClientRectangle);
            return;
        }
        var textWidth = Draw.Width(Text, font);
        var left = (Width - Theme.ArrowSize - ArrowGap - textWidth) / 2;
        Draw.Arrow(e.Graphics, ink, new Point(left + Theme.ArrowSize / 2, Height / 2), arrow);
        Draw.Text(e.Graphics, Text, font, ink, new Rectangle(left + Theme.ArrowSize + ArrowGap, 0, textWidth, Height));
    }

    private (Color Fill, Color Ink, Color? Outline) Colors()
    {
        var parent = Parent?.BackColor ?? Theme.Bg;
        if (!Enabled) return (parent, Theme.Dim, (Color?)Theme.Line);
        return _kind switch
        {
            ButtonKind.Primary => _hover ? (Theme.Line, Theme.Text, (Color?)null) : (Theme.Text, Theme.Bg, (Color?)null),
            ButtonKind.Ghost => (_hover ? Theme.Card : parent, Theme.Text, (Color?)Theme.Line),
            _ => (_hover ? Theme.CardHover : Theme.Card, Theme.Text, (Color?)null),
        };
    }
}
