namespace CoopLauncher.Gui;

/// <summary>The bottom strip: the newest player event behind a big arrow that opens and closes the full log. The words swap
/// when the event changes. It takes focus (Tab); Space and Enter toggle it.</summary>
internal sealed class LogFooter : Control
{
    private const string EventChannel = "event";
    private const string AccessibleLabel = "Log";

    private static readonly int FooterHeight = Theme.Scale(30);
    private static readonly int PadX = Theme.Scale(14);
    private static readonly int ArrowWidth = Theme.Scale(20);

    private readonly StatusLine _event;
    private bool _expanded;

    public event Action<bool>? Toggled;

    public LogFooter()
    {
        Dock = DockStyle.Bottom;
        Height = FooterHeight;
        BackColor = Theme.Bg2;
        Cursor = Cursors.Hand;
        TabStop = true;
        AccessibleName = AccessibleLabel;
        AccessibleRole = AccessibleRole.PushButton;
        _event = new StatusLine(this, EventChannel);
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer
                 | ControlStyles.ResizeRedraw | ControlStyles.Selectable | ControlStyles.StandardClick, true);
    }

    public void ShowEvent(string message) => _event.Set(message);

    protected override void OnMouseClick(MouseEventArgs e)
    {
        Toggle();
        base.OnMouseClick(e);
    }

    protected override bool IsInputKey(Keys keyData) => (keyData & Keys.KeyCode) is Keys.Space or Keys.Enter || base.IsInputKey(keyData);

    protected override void OnKeyDown(KeyEventArgs e)
    {
        if (e.KeyCode is Keys.Space or Keys.Enter)
        {
            Toggle();
            e.Handled = true;
        }
        base.OnKeyDown(e);
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
        g.Clear(BackColor);
        Draw.HorizontalLine(g, 0, Width, 0);
        Draw.Arrow(g, Theme.Text, new Point(PadX + ArrowWidth / 2, Height / 2), _expanded ? ArrowDirection.Down : ArrowDirection.Up);
        var textX = PadX + ArrowWidth + Theme.Gap;
        _event.Paint(g, new Rectangle(textX, 0, Width - textX - PadX, Height), Theme.Mono, Theme.Muted, BackColor);
        if (Focused && ShowFocusCues) Draw.Border(g, new Rectangle(0, 1, Width, Height - 1), Theme.Text);
    }

    private void Toggle()
    {
        _expanded = !_expanded;
        Invalidate();
        Toggled?.Invoke(_expanded);
    }
}
