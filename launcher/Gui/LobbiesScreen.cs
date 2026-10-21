namespace CoopLauncher.Gui;

/// <summary>The Lobbies tab: the All / Friends / Home server filter (the last one disabled, nothing behind it) and the game
/// dropdown on top, the column header, and the list of lobbies. It holds the two filters and says when they change; the window
/// works out the rows (<see cref="LobbyRowModels"/>) and hands them back.</summary>
internal sealed class LobbiesScreen : Control
{
    private const string GameLabel = "Game";
    private const string AllGames = "All games";
    private const int HomeServerIndex = 2;

    private static readonly int PadX = Theme.Scale(24);
    private static readonly int PadTop = Theme.Scale(20);
    private static readonly int Gap = Theme.Scale(16);
    private static readonly int FilterGap = Theme.Scale(12);
    private static readonly int LabelMargin = Theme.Scale(8);
    private static readonly int SelectWidth = Theme.Scale(180);
    private static readonly int HeaderHeight = Theme.Scale(20);

    private readonly SegmentTabs _filter = new(SegmentStyle.Filter, ["All", "Friends", "Home server"], HomeServerIndex);
    private readonly FlatSelect _game = new() { Width = SelectWidth };
    private readonly LobbyRows _rows;
    private readonly List<string> _gameIds = new();
    private int _headerTop;

    public LobbiesScreen(AvatarStore avatars, Func<string, SteamArt?> artOf)
    {
        _rows = new LobbyRows(avatars, artOf);
        BackColor = Theme.Bg;
        _game.Items.Add(AllGames);
        _game.SelectedIndex = 0;
        Controls.AddRange([_filter, _game, _rows]);
        _filter.TabIndex = 0;
        _game.TabIndex = 1;
        _rows.TabIndex = 2;
        _filter.Picked += _ => Changed?.Invoke();
        _game.SelectedIndexChanged += (_, _) => Changed?.Invoke();
        _rows.ActionClicked += model => ActionClicked?.Invoke(model);
        _rows.Opened += model => Opened?.Invoke(model);
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer
                 | ControlStyles.ResizeRedraw, true);
    }

    private int LabelX => _filter.Right + FilterGap + LabelMargin;

    /// <summary>The filter or the game changed.</summary>
    public event Action? Changed;

    /// <summary>A row's button was clicked: join, or update.</summary>
    public event Action<LobbyRowModel>? ActionClicked;

    /// <summary>A row was clicked: show its popup.</summary>
    public event Action<LobbyRowModel>? Opened;

    public LobbyFilter Filter => (LobbyFilter)_filter.SelectedIndex;

    /// <summary>The game the list is narrowed to, or null for every game.</summary>
    public string? GameId => _game.SelectedIndex > 0 ? _gameIds[_game.SelectedIndex - 1] : null;

    /// <summary>Lists the games in the dropdown (once, after the rail is filled).</summary>
    public void SetGames(IEnumerable<GameProfile> games)
    {
        foreach (var game in games)
        {
            _gameIds.Add(game.Id);
            _game.Items.Add(game.Name);
        }
    }

    public void Show(IReadOnlyList<LobbyRowModel> models, bool searching) => _rows.Show(models, searching);

    protected override void OnResize(EventArgs e)
    {
        var top = PadTop;
        _filter.Location = new Point(PadX, top);
        _game.Location = new Point(LabelX + Draw.Width(GameLabel, Theme.Body) + FilterGap, top);
        _headerTop = top + _filter.Height + Gap;
        var listTop = _headerTop + HeaderHeight;
        _rows.SetBounds(PadX, listTop, Math.Max(0, Width - 2 * PadX), Math.Max(0, Height - listTop));
        base.OnResize(e);
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;
        g.Clear(BackColor);
        Draw.Text(g, GameLabel, Theme.Body, Theme.Muted, new Rectangle(LabelX, _filter.Top, Draw.Width(GameLabel, Theme.Body), _filter.Height));
        var columns = LobbyRows.Columns(_rows.ClientSize.Width, HeaderHeight);
        foreach (var (title, column) in new[] { ("GAME", columns.Game), ("HOST", columns.Host), ("PLAYERS", columns.Players), ("MOD", columns.Mod), ("PING", columns.Ping) })
            Draw.Text(g, title, Theme.Label, Theme.Muted, column with { X = PadX + column.X, Y = _headerTop });
    }
}
