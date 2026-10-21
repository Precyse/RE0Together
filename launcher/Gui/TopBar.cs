namespace CoopLauncher.Gui;

/// <summary>What the top bar shows: Browse (Play and Lobbies tabs and the build), Session (the brand alone), or View (Back in
/// front of the brand while Settings or the character select is open; Settings hides).</summary>
internal enum TopBarMode { Browse, Session, View }

/// <summary>The window's top strip and title bar: the brand, the Play / Lobbies tabs and the build on the home and lobbies
/// screens, a Back button while a view is open, the update button (only while a newer build is offered), Settings and the
/// window buttons. It carries no state words and no lamp. Its empty surface is left to the window, which drags by it.</summary>
internal sealed class TopBar : Control
{
    private const string BrandText = "CO-OP";
    private const string SettingsLabel = "Settings";
    private const string BackLabel = "Back";

    private static readonly int BarHeight = Theme.Scale(44);
    private static readonly int BrandX = Theme.Scale(20);
    private static readonly int TabsGap = Theme.Scale(16);
    private static readonly int BackX = Theme.Scale(12);
    private static readonly int BackHeight = Theme.Scale(32);
    private static readonly int BackWidth = Theme.Scale(14) + Theme.ArrowSize + Theme.Scale(8) + Draw.Width(BackLabel, Theme.Strong) + Theme.Scale(14);
    private static readonly int UpdateButtonWidth = Theme.Scale(112);
    private static readonly int SettingsButtonWidth = Draw.Width(SettingsLabel, Theme.Strong) + Theme.Scale(32);
    private static readonly int ButtonHeight = Theme.Scale(28);
    private static readonly int BrandWidth = Draw.Width(BrandText, Theme.Brand);

    private readonly WindowButtons _windowButtons = new();
    private string _build = string.Empty;
    private TopBarMode _mode = TopBarMode.Browse;
    private bool _updateShown;

    public TopBar()
    {
        Dock = DockStyle.Top;
        Height = BarHeight;
        BackColor = Theme.Bg2;
        UpdateButton.Visible = false;
        BackButton.Visible = false;
        BackButton.Arrow = ArrowDirection.Left;
        Controls.AddRange([Tabs, BackButton, UpdateButton, SettingsButton, _windowButtons]);
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer
                 | ControlStyles.ResizeRedraw, true);
    }

    /// <summary>The Play and Lobbies tabs.</summary>
    public SegmentTabs Tabs { get; } = new(SegmentStyle.Tab, ["Play", "Lobbies"]);

    /// <summary>Closes the open view.</summary>
    public FlatButton BackButton { get; } = new(BackLabel, ButtonKind.Normal) { Size = new Size(BackWidth, BackHeight) };

    /// <summary>The one update button: Update to N, Downloading, Restart. Hidden while no newer build is offered.</summary>
    public FlatButton UpdateButton { get; } = new(string.Empty, ButtonKind.Ghost) { Width = UpdateButtonWidth, Height = ButtonHeight };

    /// <summary>Opens the settings view.</summary>
    public FlatButton SettingsButton { get; } = new(SettingsLabel, ButtonKind.Ghost) { Width = SettingsButtonWidth, Height = ButtonHeight };

    /// <summary>Shows what the mode has: its buttons and tabs, and the build label on the browse screens.</summary>
    public void Show(TopBarMode mode, string build)
    {
        _mode = mode;
        _build = build;
        Tabs.Visible = mode == TopBarMode.Browse;
        BackButton.Visible = mode == TopBarMode.View;
        SettingsButton.Visible = mode != TopBarMode.View;
        PerformLayout();
        Invalidate();
    }

    /// <summary>Shows the update button with this label, or hides it (null).</summary>
    public void ShowUpdate(string? label, bool enabled)
    {
        var shown = label != null;
        if (_updateShown != shown)
        {
            _updateShown = shown;
            UpdateButton.Visible = shown;
            PerformLayout();
            Invalidate();
        }
        UpdateButton.Text = label ?? string.Empty;
        UpdateButton.Enabled = enabled;
    }

    protected override void OnLayout(LayoutEventArgs e)
    {
        base.OnLayout(e);
        var top = (Height - ButtonHeight) / 2;
        SettingsButton.Location = new Point(_windowButtons.Left - Theme.Gap - SettingsButtonWidth, top);
        UpdateButton.Location = new Point((SettingsButton.Visible ? SettingsButton.Left : _windowButtons.Left) - Theme.Gap - UpdateButtonWidth, top);
        BackButton.Location = new Point(BackX, (Height - BackHeight) / 2);
        Tabs.Location = new Point(BrandX + BrandWidth + TabsGap, (Height - Tabs.Height) / 2);
    }

    protected override void WndProc(ref Message m)
    {
        base.WndProc(ref m);
        if (m.Msg == NcHitTest.WindowMessage) m.Result = NcHitTest.Transparent;
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;
        g.Clear(BackColor);
        var brandX = _mode == TopBarMode.View ? BackButton.Right + Theme.Gap * 2 : BrandX;
        Draw.Text(g, BrandText, Theme.Brand, Theme.Text, new Rectangle(brandX, 0, BrandWidth + Theme.Gap, Height));
        if (_mode == TopBarMode.Browse)
        {
            var right = (_updateShown ? UpdateButton.Left : SettingsButton.Left) - Theme.Gap * 2;
            var width = Draw.Width(_build, Theme.Mono);
            Draw.Text(g, _build, Theme.Mono, Theme.Muted, new Rectangle(right - width, 0, width, Height));
        }
        Draw.HorizontalLine(g, 0, Width, Height - 1);
    }
}
