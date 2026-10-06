namespace CoopLauncher.Gui;

/// <summary>The left rail: one row per game with its Steam capsule art. The selected row takes the neutral selection
/// fill; the rail locks while a session is open.</summary>
internal sealed class GameRail : Control
{
    private static readonly int RailWidth = Theme.Scale(220);
    private static readonly int PadX = Theme.Scale(14);
    private static readonly int HeaderHeight = Theme.Scale(36);
    private static readonly int RowHeight = Theme.Scale(100);
    private static readonly int ArtWidth = Theme.Scale(40);
    private static readonly int ArtHeight = Theme.Scale(60);
    private static readonly int ArtTextGap = Theme.Scale(12);
    private static readonly int NameSubGap = Theme.Scale(2);
    private const int NameMaxLines = 2;

    private readonly List<Entry> _games = new();
    private int _selected = -1;
    private string? _runningId;

    private sealed record Entry(GameProfile Profile, SteamArt Art)
    {
        public GameStatus Status { get; set; } = GameStatus.Unknown;
    }

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

    public GameStatus SelectedStatus => _selected >= 0 ? _games[_selected].Status : GameStatus.Unknown;

    public IEnumerable<GameProfile> Games => _games.Select(g => g.Profile);

    public void Add(GameProfile profile, SteamArt art)
    {
        _games.Add(new Entry(profile, art));
        if (_selected < 0) Select(0);
        Invalidate();
    }

    /// <summary>Selects a game by id (the game of a lobby that was joined); an unknown id changes nothing.</summary>
    public void SelectGame(string gameId)
    {
        var index = _games.FindIndex(g => g.Profile.Id == gameId);
        if (index >= 0) Select(index);
    }

    /// <summary>Sets what a game's row says about its mod.</summary>
    public void SetStatus(string gameId, GameStatus status)
    {
        foreach (var game in _games.Where(g => g.Profile.Id == gameId)) game.Status = status;
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
        var profile = _games[index].Profile;
        var art = _games[index].Art;
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

        var textX = artBox.Right + ArtTextGap;
        var textWidth = row.Right - textX - PadX;
        var sub = profile.Id == _runningId ? "Running" : $"{profile.MaxPlayers} players";
        var status = _games[index].Status;
        var statusHeight = Draw.WrappedHeight(status.Line, Theme.Small, textWidth, NameMaxLines);
        var nameHeight = Draw.WrappedHeight(profile.Name, Theme.Strong, textWidth, NameMaxLines);
        var blockHeight = nameHeight + NameSubGap + Theme.Small.Height + NameSubGap + statusHeight;
        var y = row.Y + (row.Height - blockHeight) / 2;
        Draw.Wrapped(g, profile.Name, Theme.Strong, faded ? Theme.Dim : Theme.Text, new Rectangle(textX, y, textWidth, nameHeight));
        y += nameHeight + NameSubGap;
        var quiet = selected ? Theme.Muted : Theme.Dim;
        Draw.Wrapped(g, sub, Theme.Small, quiet, new Rectangle(textX, y, textWidth, Theme.Small.Height));
        y += Theme.Small.Height + NameSubGap;
        var statusColor = status.NeedsAttention && !faded ? Theme.Armed : quiet;
        Draw.Wrapped(g, status.Line, Theme.Small, statusColor, new Rectangle(textX, y, textWidth, statusHeight));
    }
}
