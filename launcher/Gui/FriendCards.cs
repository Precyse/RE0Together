namespace CoopLauncher.Gui;

/// <summary>A friend who is hosting a lobby: the card shows their avatar, name, the game's name and the lobby's player count
/// (empty while unknown). Selected says the friend's game is the one picked in the rail: its button is the primary JOIN.</summary>
public sealed record FriendCard(ulong AvatarId, string Name, string Game, ulong LobbyId, bool Selected = false, string Count = "");

/// <summary>The friends hosting a lobby as a two-column grid of cards: avatar, the name over the game's name and player count,
/// and a Join button (the primary JOIN for the selected game). A card that stays between two updates is kept as it is (no
/// flicker); new friends enter one after the other, gone ones fade out. An avatar that arrives later fades in over the
/// placeholder.</summary>
internal sealed class FriendCards : Panel
{
    private const int Columns = 2;

    private static readonly int CardHeight = Theme.Scale(68);
    private static readonly int CardGap = Theme.Scale(12);

    private readonly AvatarStore _avatars;
    private readonly List<CardControl> _cards = new();

    public FriendCards(AvatarStore avatars)
    {
        _avatars = avatars;
        BackColor = Theme.Bg;
        Height = 0;
    }

    /// <summary>The player clicked Join on a friend's card.</summary>
    public event Action<ulong>? JoinClicked;

    public static int HeightFor(int cards)
    {
        var rows = (cards + Columns - 1) / Columns;
        return rows == 0 ? 0 : rows * CardHeight + (rows - 1) * CardGap;
    }

    /// <summary>Shows these friends in this order, keeping the cards of lobbies that were already shown.</summary>
    public void Show(IReadOnlyList<FriendCard> friends)
    {
        SuspendLayout();
        var wanted = friends.Select(friend => friend.LobbyId).ToHashSet();
        foreach (var gone in _cards.Where(card => !wanted.Contains(card.Friend.LobbyId)).ToList())
        {
            _cards.Remove(gone);
            FadeOut(gone);
        }

        var ordered = new List<CardControl>();
        var arrivals = 0;
        foreach (var friend in friends)
        {
            var card = _cards.FirstOrDefault(c => c.Friend.LobbyId == friend.LobbyId);
            if (card == null)
            {
                card = new CardControl(_avatars, friend);
                card.JoinClicked += lobbyId => JoinClicked?.Invoke(lobbyId);
                Controls.Add(card);
                Arrange(card, ordered.Count);
                Stagger.In(card, arrivals++, Durations.StaggerItem, Easing.Out, Recipes.StaggerIn);
            }
            else
            {
                card.Update(friend);
            }
            ordered.Add(card);
        }
        _cards.Clear();
        _cards.AddRange(ordered);

        Height = HeightFor(_cards.Count);
        ArrangeAll();
        ResumeLayout(false);
    }

    protected override void OnResize(EventArgs e)
    {
        ArrangeAll();
        base.OnResize(e);
    }

    private void ArrangeAll()
    {
        for (var i = 0; i < _cards.Count; i++) Arrange(_cards[i], i);
    }

    private void Arrange(CardControl card, int index)
    {
        var width = (Width - CardGap * (Columns - 1)) / Columns;
        card.SetBounds(index % Columns * (width + CardGap), index / Columns * (CardHeight + CardGap), width, CardHeight);
    }

    /// <summary>A card whose lobby is gone fades out over where it was, then goes.</summary>
    private void FadeOut(CardControl card) =>
        MotionOverlay.Play(card, Durations.Swap, Easing.Out, progress => new Layer((float)(1 - progress), 0, 0, 1, 0), Theme.Bg, done: () =>
        {
            Controls.Remove(card);
            card.Dispose();
        });

    private sealed class CardControl : Control
    {
        private const string JoinPrimaryLabel = "JOIN";
        private const string JoinGhostLabel = "Join";

        private static readonly int Pad = Theme.Scale(12);
        private static readonly int AvatarSize = Theme.Scale(44);
        private static readonly int AvatarGap = Theme.Scale(12);
        private static readonly Size ButtonSize = new(Theme.Scale(80), Theme.Scale(36));
        private static readonly int NameTop = Theme.Scale(14);

        private readonly AvatarFade _avatar;
        private readonly FlatButton _join = new(JoinGhostLabel, ButtonKind.Ghost) { Size = ButtonSize };

        public CardControl(AvatarStore avatars, FriendCard friend)
        {
            Friend = friend;
            BackColor = Theme.Card;
            _avatar = new AvatarFade(this, avatars, "avatar");
            _join.Click += (_, _) => JoinClicked?.Invoke(Friend.LobbyId);
            Controls.Add(_join);
            SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer
                     | ControlStyles.ResizeRedraw, true);
            Show(friend);
        }

        public event Action<ulong>? JoinClicked;

        public FriendCard Friend { get; private set; }

        public void Update(FriendCard friend)
        {
            if (friend == Friend) return;
            Show(friend);
        }

        protected override void OnResize(EventArgs e)
        {
            _join.Location = new Point(Width - Pad - _join.Width, (Height - _join.Height) / 2);
            base.OnResize(e);
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            var g = e.Graphics;
            g.Clear(BackColor);
            _avatar.Paint(g, new Rectangle(Pad, (Height - AvatarSize) / 2, AvatarSize, AvatarSize), Friend.Name, BackColor);
            var textX = Pad + AvatarSize + AvatarGap;
            var textWidth = Math.Max(0, _join.Left - Theme.Gap - textX);
            Draw.Text(g, Friend.Name, Theme.Strong, Theme.Text, new Rectangle(textX, NameTop, textWidth, Theme.Strong.Height));
            var gameTop = NameTop + Theme.Strong.Height;
            var countWidth = Friend.Count.Length > 0 ? Draw.Width(Friend.Count, Theme.Small) : 0;
            var gameWidth = Math.Min(Draw.Width(Friend.Game, Theme.Small), Math.Max(0, textWidth - countWidth));
            Draw.Text(g, Friend.Game, Theme.Small, Theme.Muted, new Rectangle(textX, gameTop, gameWidth, Theme.Small.Height));
            if (countWidth > 0) Draw.Text(g, Friend.Count, Theme.Small, Theme.Muted, new Rectangle(textX + gameWidth, gameTop, countWidth, Theme.Small.Height));
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing) _avatar.Dispose();
            base.Dispose(disposing);
        }

        private void Show(FriendCard friend)
        {
            Friend = friend;
            _avatar.Show(friend.AvatarId);
            _join.Kind = friend.Selected ? ButtonKind.Primary : ButtonKind.Ghost;
            _join.Text = friend.Selected ? JoinPrimaryLabel : JoinGhostLabel;
            Invalidate();
        }
    }
}
