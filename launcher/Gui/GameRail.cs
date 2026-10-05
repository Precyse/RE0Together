namespace CoopLauncher.Gui;

/// <summary>The left rail: one row per game with its Steam capsule art. The selected row takes the neutral selection
/// fill; the rail locks while a session is open.</summary>
internal sealed class GameRail : Control
{
    private const int RailWidth = 220;
    private const int PadX = 14;
    private const int HeaderHeight = 36;
    private const int RowHeight = 76;
    private const int ArtWidth = 40;
    private const int ArtHeight = 60;
    private const int NameOffsetY = -9;
    private const int SubOffsetY = 10;

    private readonly List<(GameProfile Profile, SteamArt Art)> _games = new();
    private int _selected = -1;
    private string? _runningId;

    public event Action<GameProfile>? SelectionChanged;

    public GameRail()
    {
        Dock = DockStyle.Left;
        Width = RailWidth;
        BackColor = Theme.Bg2;
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer
                 | ControlStyles.ResizeRedraw, true);
    }

    public GameProfile? Selected => _selected >= 0 ? _games[_selected].Profile : null;

    public SteamArt? SelectedArt => _selected >= 0 ? _games[_selected].Art : null;

    public void Add(GameProfile profile, SteamArt art)
    {
        _games.Add((profile, art));
        if (_selected < 0) Select(0);
        Invalidate();
    }

    /// <summary>Marks the game whose session is running (its row reads Running), or none.</summary>
    public void SetRunning(string? gameId)
    {
        if (_runningId == gameId) return;
        _runningId = gameId;
        Invalidate();
    }

    protected override void OnEnabledChanged(EventArgs e)
    {
        Invalidate();
        base.OnEnabledChanged(e);
    }

    protected override void OnMouseClick(MouseEventArgs e)
    {
        var row = (e.Y - HeaderHeight) / RowHeight;
        if (e.Y >= HeaderHeight && row < _games.Count) Select(row);
        base.OnMouseClick(e);
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;
        g.Clear(BackColor);
        Draw.Caption(g, "Games", Theme.Muted, new Point(PadX, HeaderHeight / 2 - Theme.Label.Height / 2));
        for (var i = 0; i < _games.Count; i++) PaintRow(g, i, new Rectangle(0, HeaderHeight + i * RowHeight, Width - 1, RowHeight));
        Draw.VerticalLine(g, Width - 1, 0, Height);
    }

    private void Select(int row)
    {
        if (row == _selected) return;
        _selected = row;
        Invalidate();
        SelectionChanged?.Invoke(_games[row].Profile);
    }

    private void PaintRow(Graphics g, int index, Rectangle row)
    {
        var (profile, art) = _games[index];
        var selected = index == _selected;
        var faded = !Enabled && !selected;
        if (selected)
        {
            using var fill = new SolidBrush(Theme.Selection);
            g.FillRectangle(fill, row);
        }

        var artBox = new Rectangle(row.X + PadX, row.Y + (row.Height - ArtHeight) / 2, ArtWidth, ArtHeight);
        if (art.Capsule is { } capsule && !faded) Draw.Cover(g, capsule, artBox);
        else
        {
            using var placeholder = new SolidBrush(Theme.Card);
            g.FillRectangle(placeholder, artBox);
        }

        var textX = artBox.Right + 12;
        var textWidth = row.Right - textX - PadX;
        var middle = row.Y + row.Height / 2;
        var sub = profile.Id == _runningId ? "Running" : $"{profile.MaxPlayers} players";
        Draw.Text(g, profile.Name, Theme.Strong, faded ? Theme.Dim : Theme.Text, new Rectangle(textX, middle + NameOffsetY - 10, textWidth, 20));
        Draw.Text(g, sub, Theme.Small, selected ? Theme.Muted : Theme.Dim, new Rectangle(textX, middle + SubOffsetY - 8, textWidth, 16));
    }
}
