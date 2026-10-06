namespace CoopLauncher.Gui;

/// <summary>The window's top strip: the brand on the left; the build, the state lamp and the state on the right.</summary>
internal sealed class TopBar : Control
{
    private const string BrandText = "CO-OP";
    private const string SettingsLabel = "Settings";
    private const string SettingsCloseLabel = "Back";

    private static readonly int BarHeight = Theme.Scale(44);
    private static readonly int PadX = Theme.Scale(16);
    private static readonly int UpdateButtonWidth = Theme.Scale(76);
    private static readonly int SettingsButtonWidth = Theme.Scale(84);
    private static readonly int ButtonHeight = Theme.Scale(26);

    private string _build = string.Empty;
    private string _state = string.Empty;
    private string? _available;
    private Color _lamp = Theme.Dim;

    public TopBar()
    {
        Dock = DockStyle.Top;
        Height = BarHeight;
        BackColor = Theme.Bg2;
        Controls.Add(UpdateButton);
        Controls.Add(SettingsButton);
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer
                 | ControlStyles.ResizeRedraw, true);
    }

    /// <summary>Checks the release for a newer build; enabled only while no session is open.</summary>
    public FlatButton UpdateButton { get; } = new("Update", ButtonKind.Ghost) { Width = UpdateButtonWidth, Height = ButtonHeight };

    /// <summary>Opens and closes the settings view.</summary>
    public FlatButton SettingsButton { get; } = new(SettingsLabel, ButtonKind.Ghost) { Width = SettingsButtonWidth, Height = ButtonHeight };

    /// <summary>The settings view is open: its button reads Back.</summary>
    public void ShowSettingsOpen(bool open)
    {
        SettingsButton.Text = open ? SettingsCloseLabel : SettingsLabel;
        SettingsButton.Invalidate();
    }

    protected override void OnResize(EventArgs e)
    {
        var top = (Height - ButtonHeight) / 2;
        UpdateButton.Location = new Point(Width - PadX - UpdateButtonWidth, top);
        SettingsButton.Location = new Point(UpdateButton.Left - Theme.Gap - SettingsButtonWidth, top);
        base.OnResize(e);
    }

    /// <summary>A newer launcher build on GitHub, shown beside the build; null clears it.</summary>
    public void ShowAvailable(string? text)
    {
        _available = text;
        Invalidate();
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
        var stateX = SettingsButton.Left - Theme.Gap * 2 - stateWidth;
        Draw.Text(g, _state, Theme.Label, _lamp == Theme.Dim ? Theme.Muted : Theme.Text, new Rectangle(stateX, 0, stateWidth, Height));
        var lampX = stateX - Theme.Gap - Theme.LampSize;
        Draw.Lamp(g, _lamp, lampX, middle);
        var buildWidth = Draw.Width(_build, Theme.Mono);
        var buildX = lampX - Theme.Gap * 2 - buildWidth;
        Draw.Text(g, _build, Theme.Mono, Theme.Dim, new Rectangle(buildX, 0, buildWidth, Height));
        if (_available is { } available)
        {
            var availableWidth = Draw.Width(available, Theme.Mono);
            Draw.Text(g, available, Theme.Mono, Theme.Armed, new Rectangle(buildX - Theme.Gap * 2 - availableWidth, 0, availableWidth, Height));
        }

        Draw.HorizontalLine(g, 0, Width, Height - 1);
    }
}
