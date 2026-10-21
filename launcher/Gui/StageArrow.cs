using System.Drawing.Drawing2D;

namespace CoopLauncher.Gui;

/// <summary>Which mark a <see cref="StageArrow"/> carries.</summary>
internal enum StageGlyph { Previous, Next, Swap }

/// <summary>The big square button of the character select: the arrow under a character's cutout, or the swap between the halves.
/// Your own half's is a raised card with a white arrow; the partner's is an outline with a gray one; the swap is a card with a
/// 1 px outline. The fill and ink fade to their hover colours (150 ms), a press snaps darker, and a disabled arrow is drawn in
/// the disabled gray. It is operated through its <see cref="Control.Click"/>; the stage owns the keyboard.</summary>
internal sealed class StageArrow : Control
{
    private const string HoverChannel = "hover";
    private const double PressDarken = 0.35;
    private const int ArrowWidth = 14;
    private const int ArrowHeight = 16;
    private const int SwapHalfLength = 13;
    private const int SwapHalfGap = 6;
    private const int SwapHead = 4;
    private const int SwapStroke = 2;

    private readonly StageGlyph _glyph;
    private bool _outlined;
    private double _hover;
    private bool _pressed;

    public StageArrow(StageGlyph glyph, string accessibleName)
    {
        _glyph = glyph;
        AccessibleName = accessibleName;
        AccessibleRole = AccessibleRole.PushButton;
        Cursor = Cursors.Hand;
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer
                 | ControlStyles.ResizeRedraw | ControlStyles.SupportsTransparentBackColor | ControlStyles.StandardClick, true);
        SetStyle(ControlStyles.Selectable, false);
        TabStop = false;
        BackColor = Color.Transparent;
    }

    /// <summary>Width and height in 96 dpi pixels.</summary>
    public const int Extent = 44;

    /// <summary>Drawn as an outline with a gray arrow (the partner's) instead of a raised card.</summary>
    public bool Outlined
    {
        get => _outlined;
        set
        {
            if (_outlined == value) return;
            _outlined = value;
            Invalidate();
        }
    }

    protected override void OnMouseEnter(EventArgs e)
    {
        FadeHover(1);
        base.OnMouseEnter(e);
    }

    protected override void OnMouseLeave(EventArgs e)
    {
        FadeHover(0);
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

    protected override void OnEnabledChanged(EventArgs e)
    {
        Cursor = Enabled ? Cursors.Hand : Cursors.Default;
        Motion.Cancel(this, HoverChannel);
        _hover = 0;
        _pressed = false;
        Invalidate();
        base.OnEnabledChanged(e);
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;
        var ink = Enabled ? Interpolate.Lerp(_outlined ? Theme.Muted : Theme.Text, Theme.Text, _hover) : Theme.Disabled;
        if (_outlined)
        {
            Draw.Border(g, ClientRectangle, Theme.Line);
        }
        else
        {
            var fill = Interpolate.Lerp(Theme.Card, Theme.CardHover, _hover);
            Draw.Fill(g, _pressed ? Interpolate.Lerp(Theme.CardHover, Theme.Bg, PressDarken) : fill, ClientRectangle);
            if (_glyph == StageGlyph.Swap) Draw.Border(g, ClientRectangle, Theme.Line);
        }
        if (_glyph == StageGlyph.Swap) PaintSwap(g, ink);
        else PaintArrow(g, ink);
    }

    private void PaintArrow(Graphics g, Color ink)
    {
        var width = Theme.ScaleF(ArrowWidth);
        var half = Theme.ScaleF(ArrowHeight) / 2;
        var cx = Width / 2f;
        var cy = Height / 2f;
        var forward = _glyph == StageGlyph.Next;
        var tip = forward ? cx + width / 2 : cx - width / 2;
        var back = forward ? cx - width / 2 : cx + width / 2;
        var state = g.Save();
        g.SmoothingMode = SmoothingMode.AntiAlias;
        using var brush = new SolidBrush(ink);
        g.FillPolygon(brush, new[] { new PointF(tip, cy), new PointF(back, cy - half), new PointF(back, cy + half) });
        g.Restore(state);
    }

    /// <summary>Two stacked arrows pointing opposite ways: the upper one to the left, the lower one to the right.</summary>
    private void PaintSwap(Graphics g, Color ink)
    {
        var half = Theme.ScaleF(SwapHalfLength);
        var gap = Theme.ScaleF(SwapHalfGap);
        var head = Theme.ScaleF(SwapHead);
        var cx = Width / 2f;
        var cy = Height / 2f;
        var state = g.Save();
        g.SmoothingMode = SmoothingMode.AntiAlias;
        using var pen = new Pen(ink, Theme.ScaleF(SwapStroke));
        using var brush = new SolidBrush(ink);
        g.DrawLine(pen, cx - half + head, cy - gap, cx + half, cy - gap);
        g.FillPolygon(brush, new[] { new PointF(cx - half, cy - gap), new PointF(cx - half + head * 2, cy - gap - head), new PointF(cx - half + head * 2, cy - gap + head) });
        g.DrawLine(pen, cx - half, cy + gap, cx + half - head, cy + gap);
        g.FillPolygon(brush, new[] { new PointF(cx + half, cy + gap), new PointF(cx + half - head * 2, cy + gap - head), new PointF(cx + half - head * 2, cy + gap + head) });
        g.Restore(state);
    }

    private void FadeHover(double target)
    {
        if (!Enabled) return;
        Motion.Animate(this, HoverChannel, Motion.ValueOf(this, HoverChannel) ?? _hover, target, Durations.Hover, Easing.Css, amount =>
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
}
