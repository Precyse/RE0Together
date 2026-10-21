namespace CoopLauncher.Gui;

/// <summary>The left rail: one row per game with its Steam capsule art. The selected row takes the neutral selection fill,
/// which slides between rows (tabs-sliding); hovering fades a row to the card fill. The rail locks while a session is
/// open (the other rows show their art at 35 %). It scrolls by a row per wheel notch when the games outgrow it, takes
/// focus (Up and Down move the selection) and tells assistive tools it is a list of games.</summary>
internal sealed class GameRail : Control
{
    private const string SlideChannel = "slide";
    private const string ScrollChannel = "scroll";
    private const string HoverChannel = "hover";
    private const int NameMaxLines = 2;
    private const int NoRow = -1;

    /// <summary>How much of the capsule shows on a locked row.</summary>
    private const float LockedArtAlpha = 0.35f;

    private static readonly int RailWidth = Theme.Scale(220);
    private static readonly int PadX = Theme.Scale(14);
    private static readonly int HeaderHeight = Theme.Scale(36);
    private static readonly int RowHeight = Theme.Scale(100);
    private static readonly int ArtWidth = Theme.Scale(40);
    private static readonly int ArtHeight = Theme.Scale(60);
    private static readonly int ArtTextGap = Theme.Scale(12);
    private static readonly int NameSubGap = Theme.Scale(2);
    private static readonly int ScrollThumbWidth = Theme.Scale(3);

    private readonly List<Entry> _games = new();
    private int _selected = NoRow;
    private int _hoverRow = NoRow;
    private int _scrollTarget;
    private double _slide;
    private double _scroll;
    private string? _runningId;

    private sealed record Entry(GameProfile Profile, SteamArt Art)
    {
        public GameStatus Status { get; set; } = GameStatus.Unknown;

        /// <summary>0 = at rest, 1 = hovered; fades over rail.css .12s.</summary>
        public double Hover { get; set; }
    }

    public event Action<GameProfile>? SelectionChanged;

    public GameRail()
    {
        Dock = DockStyle.Left;
        Width = RailWidth;
        BackColor = Theme.Bg2;
        TabStop = true;
        AccessibleRole = AccessibleRole.List;
        AccessibleName = "Games";
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer
                 | ControlStyles.ResizeRedraw | ControlStyles.Selectable, true);
    }

    public GameProfile? Selected => _selected >= 0 ? _games[_selected].Profile : null;

    public SteamArt? SelectedArt => _selected >= 0 ? _games[_selected].Art : null;

    public GameStatus SelectedStatus => _selected >= 0 ? _games[_selected].Status : GameStatus.Unknown;

    /// <summary>A game's artwork by id (the Lobbies tab and the popups show other games than the selected one).</summary>
    public SteamArt? ArtOf(string gameId) => _games.FirstOrDefault(g => g.Profile.Id == gameId)?.Art;

    public GameStatus StatusOf(string gameId) => _games.FirstOrDefault(g => g.Profile.Id == gameId)?.Status ?? GameStatus.Unknown;

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

    private int ViewHeight => Math.Max(0, Height - HeaderHeight);

    private int MaxScroll => Math.Max(0, (_games.Count * RowHeight - ViewHeight + RowHeight - 1) / RowHeight);

    protected override AccessibleObject CreateAccessibilityInstance() => new RailAccessibleObject(this);

    protected override void OnEnabledChanged(EventArgs e)
    {
        if (!Enabled) SetHover(NoRow);
        Invalidate();
        base.OnEnabledChanged(e);
    }

    protected override void OnResize(EventArgs e)
    {
        ScrollTo(_scrollTarget);
        base.OnResize(e);
    }

    protected override void OnMouseClick(MouseEventArgs e)
    {
        var row = RowAt(e.Y);
        if (row != NoRow) Select(row);
        base.OnMouseClick(e);
    }

    protected override void OnMouseMove(MouseEventArgs e)
    {
        SetHover(Enabled ? RowAt(e.Y) : NoRow);
        base.OnMouseMove(e);
    }

    protected override void OnMouseLeave(EventArgs e)
    {
        SetHover(NoRow);
        base.OnMouseLeave(e);
    }

    protected override void OnMouseWheel(MouseEventArgs e)
    {
        if (e.Delta != 0) ScrollTo(_scrollTarget - Math.Sign(e.Delta));
        base.OnMouseWheel(e);
    }

    protected override bool IsInputKey(Keys keyData) => (keyData & Keys.KeyCode) is Keys.Up or Keys.Down || base.IsInputKey(keyData);

    protected override void OnKeyDown(KeyEventArgs e)
    {
        if (Enabled && _games.Count > 0 && e.KeyCode is Keys.Up or Keys.Down)
        {
            Select(Math.Clamp(_selected + (e.KeyCode == Keys.Down ? 1 : -1), 0, _games.Count - 1));
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
        Draw.Caption(g, "Games", Theme.Muted, new Point(PadX, HeaderHeight / 2 - Theme.Label.Height / 2));

        var state = g.Save();
        g.SetClip(new Rectangle(0, HeaderHeight, Width, ViewHeight));
        for (var i = 0; i < _games.Count; i++)
            if (i != _selected) Draw.Fill(g, Interpolate.Lerp(BackColor, Theme.Card, _games[i].Hover), RowRect(i));
        var selection = RowRect(_slide);
        if (_selected >= 0) Draw.Fill(g, Theme.Selection, selection);
        for (var i = 0; i < _games.Count; i++) PaintContent(g, i, RowRect(i));
        if (_selected >= 0 && Focused && ShowFocusCues) Draw.Border(g, selection, Theme.Text);
        g.Restore(state);

        Draw.ScrollThumb(g, new Rectangle(0, HeaderHeight, Width - 1, ViewHeight), _games.Count * RowHeight,
            (int)Math.Round(_scroll * RowHeight), ScrollThumbWidth);
        Draw.VerticalLine(g, Width - 1, 0, Height);
    }

    /// <summary>Where the row at the (fractional) position sits, given the scroll.</summary>
    private Rectangle RowRect(double position) => new(0, HeaderHeight + (int)Math.Round((position - _scroll) * RowHeight), Width - 1, RowHeight);

    private int RowAt(int y)
    {
        if (y < HeaderHeight) return NoRow;
        var row = (int)Math.Floor((y - HeaderHeight) / (double)RowHeight + _scroll);
        return row < _games.Count ? row : NoRow;
    }

    private void Select(int row)
    {
        if (row == _selected) return;
        var first = _selected < 0;
        _selected = row;
        if (first)
        {
            _slide = row;
        }
        else
        {
            Motion.Animate(this, SlideChannel, Motion.ValueOf(this, SlideChannel) ?? _slide, row, Durations.Tabs, Easing.Out, position =>
            {
                _slide = position;
                Invalidate();
            });
        }
        EnsureVisible(row);
        Invalidate();
        SelectionChanged?.Invoke(_games[row].Profile);
    }

    private void EnsureVisible(int row)
    {
        var visibleRows = Math.Max(1, ViewHeight / RowHeight);
        if (row < _scrollTarget) ScrollTo(row);
        else if (row >= _scrollTarget + visibleRows) ScrollTo(row - visibleRows + 1);
    }

    private void ScrollTo(int row)
    {
        _scrollTarget = Math.Clamp(row, 0, MaxScroll);
        Motion.Animate(this, ScrollChannel, Motion.ValueOf(this, ScrollChannel) ?? _scroll, _scrollTarget, Durations.Tabs, Easing.Out,
            position =>
            {
                _scroll = position;
                Invalidate();
            });
    }

    private void SetHover(int row)
    {
        if (_hoverRow == row) return;
        if (_hoverRow >= 0 && _hoverRow < _games.Count) FadeHover(_hoverRow, 0);
        _hoverRow = row;
        if (row >= 0) FadeHover(row, 1);
    }

    private void FadeHover(int row, double to)
    {
        var game = _games[row];
        var channel = HoverChannel + row;
        Motion.Animate(this, channel, Motion.ValueOf(this, channel) ?? game.Hover, to, Durations.RailHover, Easing.Css, amount =>
        {
            game.Hover = amount;
            Invalidate();
        });
    }

    private void PaintContent(Graphics g, int index, Rectangle row)
    {
        if (row.Bottom <= HeaderHeight || row.Y >= Height) return;
        var profile = _games[index].Profile;
        var art = _games[index].Art;
        var locked = RailModel.Locked(Enabled, index == _selected);

        var artBox = new Rectangle(row.X + PadX, row.Y + (row.Height - ArtHeight) / 2, ArtWidth, ArtHeight);
        if (art.Capsule is not { } capsule) Draw.Fill(g, Theme.Card, artBox);
        else if (locked) Draw.Faded(g, capsule, artBox, LockedArtAlpha);
        else Draw.Cover(g, capsule, artBox);

        var textX = artBox.Right + ArtTextGap;
        var textWidth = row.Right - textX - PadX;
        var sub = RailModel.Sub(profile, _runningId);
        var status = _games[index].Status;
        var statusHeight = Draw.WrappedHeight(status.Line, Theme.Small, textWidth, NameMaxLines);
        var nameHeight = Draw.WrappedHeight(profile.Name, Theme.Strong, textWidth, NameMaxLines);
        var blockHeight = nameHeight + NameSubGap + Theme.Small.Height + NameSubGap + statusHeight;
        var y = row.Y + (row.Height - blockHeight) / 2;
        Draw.Wrapped(g, profile.Name, Theme.Strong, locked ? Theme.Dim : Theme.Text, new Rectangle(textX, y, textWidth, nameHeight));
        y += nameHeight + NameSubGap;
        var quiet = Interpolate.Lerp(Theme.Dim, Theme.Muted, Math.Clamp(1 - Math.Abs(_slide - index), 0, 1));
        Draw.Wrapped(g, sub, Theme.Small, quiet, new Rectangle(textX, y, textWidth, Theme.Small.Height));
        y += Theme.Small.Height + NameSubGap;
        var tone = RailModel.Tone(status);
        var statusColor = locked || tone == RailTone.Quiet ? quiet : tone == RailTone.Red ? Theme.Problem : Theme.Armed;
        Draw.Wrapped(g, status.Line, Theme.Small, statusColor, new Rectangle(textX, y, textWidth, statusHeight));
    }

    private sealed class RailAccessibleObject : ControlAccessibleObject
    {
        private readonly GameRail _rail;

        public RailAccessibleObject(GameRail rail) : base(rail) => _rail = rail;

        public override AccessibleRole Role => AccessibleRole.List;

        public override int GetChildCount() => _rail._games.Count;

        public override AccessibleObject GetChild(int index) => new RowAccessibleObject(this, _rail, index);
    }

    private sealed class RowAccessibleObject : AccessibleObject
    {
        private readonly AccessibleObject _parent;
        private readonly GameRail _rail;
        private readonly int _index;

        public RowAccessibleObject(AccessibleObject parent, GameRail rail, int index)
        {
            _parent = parent;
            _rail = rail;
            _index = index;
        }

        public override AccessibleObject Parent => _parent;

        public override AccessibleRole Role => AccessibleRole.ListItem;

        public override string? Name
        {
            get => _rail._games[_index].Profile.Name;
            set { }
        }

        public override Rectangle Bounds => _rail.RectangleToScreen(_rail.RowRect(_index));

        public override AccessibleStates State => AccessibleStates.Selectable | AccessibleStates.Focusable
            | (_index == _rail._selected ? AccessibleStates.Selected | AccessibleStates.Focused : AccessibleStates.None);

        public override void Select(AccessibleSelection flags)
        {
            if (_rail.Enabled && flags.HasFlag(AccessibleSelection.TakeSelection)) _rail.Select(_index);
        }
    }
}
