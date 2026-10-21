namespace CoopLauncher.Gui;

/// <summary>The window's top strip and title bar: the brand on the left; the build, the state lamp and words, the update
/// button (only while a newer build is offered), Settings and the window buttons on the right. The state words swap and
/// their numbers roll; the lamp changes at once. Its empty surface is left to the window, which drags by it.</summary>
internal sealed class TopBar : Control
{
    private const string BrandText = "CO-OP";
    private const string SettingsLabel = "Settings";
    private const string BackLabel = "Back";
    private const string StateChannel = "state";

    private static readonly int BarHeight = Theme.Scale(44);
    private static readonly int PadX = Theme.Scale(16);
    private static readonly int UpdateButtonWidth = Theme.Scale(112);
    private static readonly int SettingsButtonWidth = Theme.Scale(84);
    private static readonly int ButtonHeight = Theme.Scale(26);
    private static readonly int BrandRight = PadX + Draw.Width(BrandText, Theme.Brand);

    private readonly StatusLine _state;
    private readonly WindowButtons _windowButtons = new();
    private string _build = string.Empty;
    private Color _lamp = Theme.Dim;
    private bool _updateShown;

    public TopBar()
    {
        Dock = DockStyle.Top;
        Height = BarHeight;
        BackColor = Theme.Bg2;
        _state = new StatusLine(this, StateChannel);
        UpdateButton.Visible = false;
        Controls.Add(UpdateButton);
        Controls.Add(SettingsButton);
        Controls.Add(_windowButtons);
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer
                 | ControlStyles.ResizeRedraw, true);
    }

    /// <summary>The one update button: Update to N, Downloading, Restart. Hidden while no newer build is offered.</summary>
    public FlatButton UpdateButton { get; } = new(string.Empty, ButtonKind.Ghost) { Width = UpdateButtonWidth, Height = ButtonHeight };

    /// <summary>Opens the settings view, or closes whichever view is open.</summary>
    public FlatButton SettingsButton { get; } = new(SettingsLabel, ButtonKind.Ghost) { Width = SettingsButtonWidth, Height = ButtonHeight };

    /// <summary>A view (settings, character select) is open: the button reads Back.</summary>
    public void ShowBack(bool open)
    {
        SettingsButton.Text = open ? BackLabel : SettingsLabel;
        SettingsButton.Arrow = open ? ArrowDirection.Left : null;
        SettingsButton.Invalidate();
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

    public void Show(string build, string state, Color lamp)
    {
        _build = build;
        _lamp = lamp;
        _state.Set(state.ToUpperInvariant());
        Invalidate();
    }

    protected override void OnLayout(LayoutEventArgs e)
    {
        base.OnLayout(e);
        var top = (Height - ButtonHeight) / 2;
        SettingsButton.Location = new Point(_windowButtons.Left - Theme.Gap - SettingsButtonWidth, top);
        UpdateButton.Location = new Point(SettingsButton.Left - Theme.Gap - UpdateButtonWidth, top);
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
        var middle = Height / 2;
        Draw.Text(g, BrandText, Theme.Brand, Theme.Text, new Rectangle(PadX, 0, Width / 2, Height));

        // The words are right-aligned in all the room between the brand and the buttons, so no state (a refusal naming a
        // file, a long one swapping out) is ever cut.
        var slotLeft = BrandRight + Theme.Gap * 2;
        var slotRight = (_updateShown ? UpdateButton.Left : SettingsButton.Left) - Theme.Gap * 2;
        var slot = new Rectangle(slotLeft, 0, Math.Max(0, slotRight - slotLeft), Height);
        _state.Paint(g, slot, Theme.Label, _lamp == Theme.Dim ? Theme.Muted : Theme.Text, BackColor, alignRight: true);
        var lampX = slot.Right - Draw.Width(_state.Text, Theme.Label) - Theme.Gap - Theme.LampSize;
        Draw.Lamp(g, _lamp, lampX, middle);
        var buildWidth = Draw.Width(_build, Theme.Mono);
        Draw.Text(g, _build, Theme.Mono, Theme.Dim, new Rectangle(lampX - Theme.Gap * 2 - buildWidth, 0, buildWidth, Height));

        Draw.HorizontalLine(g, 0, Width, Height - 1);
    }
}
