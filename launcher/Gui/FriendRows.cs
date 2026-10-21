namespace CoopLauncher.Gui;

/// <summary>A friend who is hosting a lobby: the row shows their avatar, name and the game's name.</summary>
public sealed record FriendCard(ulong AvatarId, string Name, string Game, ulong LobbyId);

/// <summary>The friends hosting a lobby, one row each with a Join button: avatar, the name over the game's name, a real
/// separator between rows. A row that stays between two updates is kept as it is (no flicker); new friends are added,
/// gone ones removed. An avatar that arrives later fades in over the placeholder.</summary>
internal sealed class FriendRows : Panel
{
    private static readonly int RowHeight = Theme.Scale(44);
    private static readonly int AvatarSize = Theme.Scale(28);
    private static readonly int AvatarGap = Theme.Scale(12);
    private static readonly int NameTop = Theme.Scale(4);
    private static readonly int GameTop = Theme.Scale(22);
    private static readonly int LineHeight = Theme.Scale(18);

    private readonly AvatarStore _avatars;
    private readonly List<Row> _rows = new();

    public FriendRows(AvatarStore avatars)
    {
        _avatars = avatars;
        BackColor = Theme.Bg;
        Height = 0;
    }

    /// <summary>The player clicked Join on the friend's row.</summary>
    public event Action<ulong>? JoinClicked;

    public static int HeightFor(int rows) => rows * RowHeight;

    /// <summary>Shows these friends in this order, keeping the rows of lobbies that were already shown.</summary>
    public void Show(IReadOnlyList<FriendCard> cards)
    {
        SuspendLayout();
        var wanted = cards.Select(card => card.LobbyId).ToHashSet();
        foreach (var gone in _rows.Where(row => !wanted.Contains(row.Card.LobbyId)).ToList())
        {
            _rows.Remove(gone);
            Controls.Remove(gone);
            gone.Dispose();
        }

        var ordered = new List<Row>();
        foreach (var card in cards)
        {
            var row = _rows.FirstOrDefault(r => r.Card.LobbyId == card.LobbyId);
            if (row == null)
            {
                row = new Row(_avatars, card);
                row.JoinClicked += lobbyId => JoinClicked?.Invoke(lobbyId);
                Controls.Add(row);
            }
            else
            {
                row.Update(card);
            }
            ordered.Add(row);
        }
        _rows.Clear();
        _rows.AddRange(ordered);

        Height = HeightFor(_rows.Count);
        Arrange();
        ResumeLayout(false);
    }

    protected override void OnResize(EventArgs e)
    {
        Arrange();
        base.OnResize(e);
    }

    private void Arrange()
    {
        for (var i = 0; i < _rows.Count; i++)
        {
            _rows[i].SetBounds(0, i * RowHeight, Width, RowHeight);
            _rows[i].Separated = i > 0;
        }
    }

    private sealed class Row : Control
    {
        private readonly FlatButton _join = new("Join", ButtonKind.Ghost) { Width = Theme.ToolWidth };
        private readonly AvatarFade _avatar;
        private bool _separated;

        public Row(AvatarStore avatars, FriendCard card)
        {
            Card = card;
            BackColor = Theme.Bg;
            _avatar = new AvatarFade(this, avatars, "avatar");
            _avatar.Show(card.AvatarId);
            _join.Click += (_, _) => JoinClicked?.Invoke(Card.LobbyId);
            Controls.Add(_join);
            SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer
                     | ControlStyles.ResizeRedraw, true);
        }

        public event Action<ulong>? JoinClicked;

        public FriendCard Card { get; private set; }

        public bool Separated
        {
            get => _separated;
            set
            {
                if (_separated == value) return;
                _separated = value;
                Invalidate();
            }
        }

        public void Update(FriendCard card)
        {
            Card = card;
            _avatar.Show(card.AvatarId);
            Invalidate();
        }

        protected override void OnResize(EventArgs e)
        {
            _join.Location = new Point(Width - _join.Width, (Height - _join.Height) / 2);
            base.OnResize(e);
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            var g = e.Graphics;
            g.Clear(BackColor);
            if (_separated) Draw.HorizontalLine(g, 0, Width, 0);
            _avatar.Paint(g, new Rectangle(0, (Height - AvatarSize) / 2, AvatarSize, AvatarSize), Card.Name, BackColor);
            var textX = AvatarSize + AvatarGap;
            var textWidth = Math.Max(0, _join.Left - Theme.Gap - textX);
            Draw.Text(g, Card.Name, Theme.Strong, Theme.Text, new Rectangle(textX, NameTop, textWidth, LineHeight));
            Draw.Text(g, Card.Game, Theme.Small, Theme.Muted, new Rectangle(textX, GameTop, textWidth, LineHeight));
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing) _avatar.Dispose();
            base.Dispose(disposing);
        }
    }
}
