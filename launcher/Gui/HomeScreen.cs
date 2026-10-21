namespace CoopLauncher.Gui;

/// <summary>The home screen (the Play tab): the game rail on the left, and beside it the pane with the hero (art, logo and the
/// action row) and the FRIENDS PLAYING cards under it. The pane scrolls once the cards outgrow the window. It composes; what
/// each part shows is worked out in <see cref="SessionCards"/>.</summary>
internal sealed class HomeScreen : Control
{
    private const string FriendsCaption = "FRIENDS PLAYING";

    private static readonly int Pad = Theme.Scale(24);
    private static readonly int CaptionTop = Theme.Scale(8);
    private static readonly int CaptionGap = Theme.Scale(8);
    private static readonly int SectionBottom = Theme.Scale(20);
    private static readonly int CaptionSpacing = Theme.Scale(1);

    private readonly Panel _pane = new() { Dock = DockStyle.Fill, BackColor = Theme.Bg, AutoScroll = true };
    private readonly HeroBanner _hero = new();
    private readonly FriendsSection _friends;

    public HomeScreen(AvatarStore avatars)
    {
        _friends = new FriendsSection(avatars);
        BackColor = Theme.Bg;
        NativeTheme.DarkScrollbars(_pane);
        _pane.Controls.Add(_friends);
        _pane.Controls.Add(_hero);
        Controls.Add(_pane);
        Controls.Add(Rail);
        Rail.TabIndex = 0;
        _pane.TabIndex = 1;
        _hero.HostClicked += () => HostClicked?.Invoke();
        _hero.ModClicked += () => ModClicked?.Invoke();
        _hero.JoinRequested += text => JoinRequested?.Invoke(text);
        _hero.OptionsClicked += () => OptionsClicked?.Invoke();
        _hero.CharactersClicked += () => CharactersClicked?.Invoke();
        _hero.NoticeActionClicked += () => NoticeActionClicked?.Invoke();
        _hero.NoticeReportClicked += () => NoticeReportClicked?.Invoke();
        _friends.JoinClicked += lobbyId => FriendJoinClicked?.Invoke(lobbyId);
        _hero.Resize += (_, _) => FitPane();
        _friends.Resize += (_, _) => FitPane();
        _pane.Resize += (_, _) => FitPane();
    }

    /// <summary>The games, left of the pane.</summary>
    public GameRail Rail { get; } = new();

    public event Action? HostClicked;
    public event Action? ModClicked;
    public event Action<string>? JoinRequested;
    public event Action? OptionsClicked;
    public event Action? CharactersClicked;
    public event Action? NoticeActionClicked;
    public event Action? NoticeReportClicked;
    public event Action<ulong>? FriendJoinClicked;

    /// <summary>The options cog reads at full ink while its popup is open.</summary>
    public bool OptionsOpen
    {
        set => _hero.OptionsOpen = value;
    }

    /// <summary>The typed lobby code was refused: the field shakes.</summary>
    public void RefuseCode() => _hero.RefuseCode();

    public void Show(HomeView view, GameProfile game, SteamArt art, bool hasOptions)
    {
        _hero.Show(view, game, art, hasOptions);
        _friends.Show(view.Friends);
        FitPane();
    }

    private void FitPane() => _pane.AutoScrollMinSize = new Size(0, _hero.Height + _friends.Height);

    /// <summary>The FRIENDS PLAYING caption over the cards; takes no room without friends.</summary>
    private sealed class FriendsSection : Control
    {
        private readonly FriendCards _cards;

        public FriendsSection(AvatarStore avatars)
        {
            _cards = new FriendCards(avatars);
            Dock = DockStyle.Top;
            BackColor = Theme.Bg;
            Controls.Add(_cards);
            _cards.JoinClicked += lobbyId => JoinClicked?.Invoke(lobbyId);
            SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer
                     | ControlStyles.ResizeRedraw, true);
        }

        public event Action<ulong>? JoinClicked;

        public void Show(IReadOnlyList<FriendCard> friends)
        {
            _cards.Show(friends);
            Height = friends.Count == 0 ? 0 : CaptionTop + Theme.Label.Height + CaptionGap + _cards.Height + SectionBottom;
            Arrange();
            Invalidate();
        }

        protected override void OnResize(EventArgs e)
        {
            Arrange();
            base.OnResize(e);
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            e.Graphics.Clear(BackColor);
            if (_cards.Height == 0) return;
            Draw.TextSpaced(e.Graphics, FriendsCaption, Theme.Label, Theme.Muted, new Rectangle(Pad, CaptionTop, Width - 2 * Pad, Theme.Label.Height), CaptionSpacing);
        }

        private void Arrange() => _cards.SetBounds(Pad, CaptionTop + Theme.Label.Height + CaptionGap, Math.Max(0, Width - 2 * Pad), _cards.Height);
    }
}
