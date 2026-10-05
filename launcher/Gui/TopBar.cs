namespace CoopLauncher.Gui;

/// <summary>The window's top strip: the brand on the left; the build, the state lamp and the state on the right.</summary>
internal sealed class TopBar : Control
{
    private const int BarHeight = 44;
    private const int PadX = 16;
    private const string BrandText = "CO-OP";

    private string _build = string.Empty;
    private string _state = string.Empty;
    private Color _lamp = Theme.Dim;

    public TopBar()
    {
        Dock = DockStyle.Top;
        Height = BarHeight;
        BackColor = Theme.Bg2;
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer
                 | ControlStyles.ResizeRedraw, true);
    }

    public void Show(string build, string state, Color lamp)
    {
        _build = build;
        _state = state.ToUpperInvariant();
        _lamp = lamp;
        Invalidate();
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;
        g.Clear(BackColor);
        var middle = Height / 2;
        Draw.Text(g, BrandText, Theme.Brand, Theme.Text, new Rectangle(PadX, 0, Width / 2, Height));

        var stateWidth = Draw.Width(_state, Theme.Label);
        var stateX = Width - PadX - stateWidth;
        Draw.Text(g, _state, Theme.Label, _lamp == Theme.Dim ? Theme.Muted : Theme.Text, new Rectangle(stateX, 0, stateWidth, Height));
        var lampX = stateX - Theme.Gap - Theme.LampSize;
        Draw.Lamp(g, _lamp, lampX, middle);
        var buildWidth = Draw.Width(_build, Theme.Mono);
        Draw.Text(g, _build, Theme.Mono, Theme.Dim, new Rectangle(lampX - Theme.Gap * 2 - buildWidth, 0, buildWidth, Height));

        Draw.HorizontalLine(g, 0, Width, Height - 1);
    }
}
