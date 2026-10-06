namespace CoopLauncher.Gui;

/// <summary>The bottom strip: the newest log line behind an arrow that opens and closes the full log.</summary>
internal sealed class LogFooter : Control
{
    private const int FooterHeight = 30;
    private const int PadX = 14;
    private const int ArrowWidth = 18;
    private const string ArrowOpen = "▲";
    private const string ArrowClose = "▼";
    private static readonly Font ArrowFont = new("Segoe UI", 9f, FontStyle.Bold);

    private string _line = string.Empty;
    private bool _expanded;

    public event Action<bool>? Toggled;

    public LogFooter()
    {
        Dock = DockStyle.Bottom;
        Height = FooterHeight;
        BackColor = Theme.Bg2;
        Cursor = Cursors.Hand;
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer
                 | ControlStyles.ResizeRedraw, true);
    }

    public void ShowLine(string line)
    {
        _line = line;
        Invalidate();
    }

    protected override void OnMouseClick(MouseEventArgs e)
    {
        _expanded = !_expanded;
        Invalidate();
        Toggled?.Invoke(_expanded);
        base.OnMouseClick(e);
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;
        g.Clear(BackColor);
        Draw.HorizontalLine(g, 0, Width, 0);
        Draw.Text(g, _expanded ? ArrowClose : ArrowOpen, ArrowFont, Theme.Text, new Rectangle(PadX, 0, ArrowWidth, Height));
        var textX = PadX + ArrowWidth + Theme.Gap;
        Draw.Text(g, _line, Theme.Mono, Theme.Muted, new Rectangle(textX, 0, Width - textX - PadX, Height));
    }
}
