namespace CoopLauncher.Gui;

/// <summary>The window's own minimize and close buttons at the right of the top strip: plain line glyphs, a grey fill
/// on hover (close a darker one) that fades in and out (controls.css .15s). The top resize band above them stays with the window's frame.</summary>
internal sealed class WindowButtons : Control
{
    private const int ButtonCount = 2;
    private const int MinimizeIndex = 0;
    private const int CloseIndex = 1;
    private const int NoButton = -1;
    private const string HoverChannel = "hover";

    private static readonly int ButtonWidth = Theme.Scale(46);
    private static readonly int GlyphSize = Theme.Scale(10);

    private readonly double[] _hover = new double[ButtonCount];
    private int _hovered = NoButton;

    public WindowButtons()
    {
        Dock = DockStyle.Right;
        Width = ButtonWidth * ButtonCount;
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer
                 | ControlStyles.ResizeRedraw, true);
    }

    protected override void WndProc(ref Message m)
    {
        base.WndProc(ref m);
        if (m.Msg == NcHitTest.WindowMessage && InResizeBand(PointToClient(NcHitTest.ScreenPoint(m)))) m.Result = NcHitTest.Transparent;
    }

    protected override void OnMouseMove(MouseEventArgs e)
    {
        SetHover(Math.Clamp(e.X / ButtonWidth, 0, ButtonCount - 1));
        base.OnMouseMove(e);
    }

    protected override void OnMouseLeave(EventArgs e)
    {
        SetHover(NoButton);
        base.OnMouseLeave(e);
    }

    protected override void OnMouseClick(MouseEventArgs e)
    {
        base.OnMouseClick(e);
        if (e.Button != MouseButtons.Left || FindForm() is not { } form) return;
        if (e.X / ButtonWidth == MinimizeIndex) form.WindowState = FormWindowState.Minimized;
        else form.Close();
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;
        var rest = Parent?.BackColor ?? Theme.Bg2;
        g.Clear(rest);
        for (var index = 0; index < ButtonCount; index++)
        {
            var cell = new Rectangle(index * ButtonWidth, 0, ButtonWidth, Height);
            if (_hover[index] > 0)
                Draw.Fill(g, Interpolate.Lerp(rest, index == CloseIndex ? Theme.Bg : Theme.Card, _hover[index]), cell);
            PaintGlyph(g, index, new Point(cell.X + cell.Width / 2, cell.Height / 2));
        }
    }

    private static void PaintGlyph(Graphics g, int index, Point center)
    {
        var half = GlyphSize / 2;
        using var pen = new Pen(Theme.Text);
        if (index == MinimizeIndex)
        {
            g.DrawLine(pen, center.X - half, center.Y, center.X + half, center.Y);
            return;
        }
        g.DrawLine(pen, center.X - half, center.Y - half, center.X + half, center.Y + half);
        g.DrawLine(pen, center.X - half, center.Y + half, center.X + half, center.Y - half);
    }

    private bool InResizeBand(Point point) =>
        point.Y < Theme.ResizeBand && FindForm() is { WindowState: not FormWindowState.Maximized };

    private void SetHover(int index)
    {
        if (_hovered == index) return;
        if (_hovered != NoButton) Fade(_hovered, 0);
        _hovered = index;
        if (index != NoButton) Fade(index, 1);
    }

    private void Fade(int index, double to) => Motion.Animate(this, HoverChannel + index, Motion.ValueOf(this, HoverChannel + index) ?? _hover[index],
        to, Durations.Hover, Easing.Css, amount =>
        {
            _hover[index] = amount;
            Invalidate();
        });
}
