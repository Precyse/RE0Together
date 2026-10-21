namespace CoopLauncher.Gui;

/// <summary>The panel of the options popup (opened from the cog on the home hero): the game's art with its logo and a close button,
/// then the game's session options as label and dropdown rows (RE0: Partner mode; SR2: Network, Players). With the host's values and
/// locked, the rows read the host's choices and cannot be changed. Shown by the <see cref="ModalLayer"/>; it reports a change and
/// the close button.</summary>
internal sealed class OptionsPopup : Control
{
    private static readonly int PanelWidth = Theme.Scale(420);
    private static readonly int PadX = Theme.Scale(20);
    private static readonly int PadTop = Theme.Scale(16);
    private static readonly int PadBottom = Theme.Scale(20);

    private readonly PopupHeader _header = new();
    private readonly OptionsRows _rows = new(columns: 1, background: Theme.Bg2);

    public OptionsPopup(GameProfile game, string? gameDir, IReadOnlyDictionary<string, string>? hostValues, bool locked, SteamArt art)
    {
        BackColor = Theme.Bg2;
        _rows.Dock = DockStyle.None;
        _rows.Show(game, gameDir, hostValues);
        _rows.SetLocked(locked);
        _rows.Location = new Point(PadX, _header.Height + PadTop);
        _rows.Width = PanelWidth - 2 * PadX;
        _header.Show(art);
        Controls.AddRange([_rows, _header]);
        Size = new Size(PanelWidth, _header.Height + PadTop + _rows.Height + PadBottom);
        _rows.Changed += () => Changed?.Invoke();
        _header.CloseClicked += () => CloseClicked?.Invoke();
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer
                 | ControlStyles.ResizeRedraw, true);
    }

    /// <summary>An option was changed (it is saved already).</summary>
    public event Action? Changed;

    /// <summary>The close button was clicked.</summary>
    public event Action? CloseClicked;

    protected override void OnPaint(PaintEventArgs e)
    {
        e.Graphics.Clear(BackColor);
        Draw.Border(e.Graphics, ClientRectangle, Theme.Line);
    }
}
