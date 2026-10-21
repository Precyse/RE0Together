namespace CoopLauncher.Gui;

/// <summary>The scrolling list of the Lobbies tab: one painted 56 px row per lobby, 6 px apart. A row that stays between two
/// searches is kept (no flicker); new lobbies come in staggered, gone ones fade out. While the first search has found
/// nothing yet the list shows four skeleton rows pulsing; the lobbies then reveal over them. Clicking a row opens its popup,
/// its button joins (or updates).</summary>
internal sealed class LobbyRows : Panel
{
    private const string PulseChannel = "skeleton";
    private const int PulseMs = 1000;
    private const int SkeletonRows = 4;
    private const double PulseLow = 0.5;
    private const double GameFraction = 2.2;
    private const double HostFraction = 1.2;
    private const double SmallFraction = 0.7;
    private const double TotalFractions = GameFraction + HostFraction + 3 * SmallFraction;
    private const int ColumnCount = 5;

    private static readonly int RowHeight = Theme.Scale(56);
    private static readonly int RowGap = Theme.Scale(6);
    private static readonly int BottomPad = Theme.Scale(20);
    private static readonly int Pad = Theme.Scale(12);
    private static readonly int ColumnGap = Theme.Scale(12);
    private static readonly int ButtonWidth = Theme.Scale(92);

    private readonly AvatarStore _avatars;
    private readonly Func<string, SteamArt?> _artOf;
    private readonly List<LobbyRow> _rows = new();
    private bool _skeleton;
    private double _pulse = 1;

    public LobbyRows(AvatarStore avatars, Func<string, SteamArt?> artOf)
    {
        _avatars = avatars;
        _artOf = artOf;
        BackColor = Theme.Bg;
        AutoScroll = true;
        NativeTheme.DarkScrollbars(this);
        SetStyle(ControlStyles.OptimizedDoubleBuffer, true);
    }

    /// <summary>The button of a row was clicked: join, or update.</summary>
    public event Action<LobbyRowModel>? ActionClicked;

    /// <summary>The row (not its button) was clicked: open the popup.</summary>
    public event Action<LobbyRowModel>? Opened;

    /// <summary>Applies a lobby's action to a button: the primary JOIN, a disabled Full, Update, or a disabled Join.</summary>
    public static void Configure(FlatButton button, LobbyAction action)
    {
        var shown = LobbyRowModels.ButtonOf(action);
        button.Kind = shown.Primary ? ButtonKind.Primary : ButtonKind.Ghost;
        button.Text = shown.Label;
        button.Enabled = shown.Enabled;
    }

    /// <summary>The columns of a row (and of the header over the rows) for a width: the left edge and width of each of game,
    /// host, players, mod and ping, and the button's left edge.</summary>
    public static (Rectangle Game, Rectangle Host, Rectangle Players, Rectangle Mod, Rectangle Ping, int Button) Columns(int width, int height)
    {
        var flex = Math.Max(0, width - 2 * Pad - ButtonWidth - ColumnCount * ColumnGap);
        var unit = flex / TotalFractions;
        var x = Pad;
        Rectangle Next(double fraction)
        {
            var rect = new Rectangle(x, 0, (int)(unit * fraction), height);
            x += rect.Width + ColumnGap;
            return rect;
        }
        return (Next(GameFraction), Next(HostFraction), Next(SmallFraction), Next(SmallFraction), Next(SmallFraction), width - Pad - ButtonWidth);
    }

    /// <summary>Shows these rows in this order. With none and a search running, the skeleton shows.</summary>
    public void Show(IReadOnlyList<LobbyRowModel> models, bool searching)
    {
        SuspendLayout();
        var wanted = models.Select(model => model.Listing.LobbyId).ToHashSet();
        foreach (var gone in _rows.Where(row => !wanted.Contains(row.Model.Listing.LobbyId)).ToList())
        {
            _rows.Remove(gone);
            FadeOut(gone);
        }

        var ordered = new List<LobbyRow>();
        var arrivals = 0;
        foreach (var model in models)
        {
            var row = _rows.FirstOrDefault(r => r.Model.Listing.LobbyId == model.Listing.LobbyId);
            if (row == null)
            {
                row = new LobbyRow(_avatars, model, _artOf(model.Game.Id));
                row.ActionClicked += m => ActionClicked?.Invoke(m);
                row.Opened += m => Opened?.Invoke(m);
                Controls.Add(row);
                row.SetBounds(0, ordered.Count * (RowHeight + RowGap), Math.Max(0, ClientSize.Width), RowHeight);
                Stagger.In(row, arrivals++, Durations.Reveal, Easing.InOut, Recipes.RevealIn);
            }
            else
            {
                row.Update(model);
            }
            ordered.Add(row);
        }
        _rows.Clear();
        _rows.AddRange(ordered);
        SetSkeleton(searching && _rows.Count == 0);
        ArrangeAll();
        ResumeLayout(false);
    }

    protected override void OnResize(EventArgs e)
    {
        ArrangeAll();
        base.OnResize(e);
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        base.OnPaint(e);
        if (!_skeleton) return;
        var g = e.Graphics;
        var width = ClientSize.Width;
        for (var i = 0; i < SkeletonRows; i++)
        {
            var row = new Rectangle(0, i * (RowHeight + RowGap), width, RowHeight);
            Draw.Fill(g, Color.FromArgb((int)(255 * _pulse), Theme.Card), row);
            PaintSkeletonBars(g, row);
        }
    }

    /// <summary>The shapes a row will fill: the capsule, the game name, the host and the numbers.</summary>
    private static void PaintSkeletonBars(Graphics g, Rectangle row)
    {
        var columns = Columns(row.Width, row.Height);
        var capsule = new Rectangle(columns.Game.X, row.Y + (row.Height - LobbyRow.CapsuleHeight) / 2, LobbyRow.CapsuleWidth, LobbyRow.CapsuleHeight);
        Draw.Fill(g, Theme.Selection, capsule);
        var barHeight = Theme.Scale(12);
        foreach (var (left, width) in new[]
                 {
                     (capsule.Right + Theme.Scale(10), Theme.Scale(120)), (columns.Host.X, Theme.Scale(80)), (columns.Players.X, Theme.Scale(30)),
                     (columns.Mod.X, Theme.Scale(36)), (columns.Ping.X, Theme.Scale(44)),
                 })
            Draw.Fill(g, Theme.Selection, new Rectangle(left, row.Y + (row.Height - barHeight) / 2, width, barHeight));
    }

    private void SetSkeleton(bool on)
    {
        if (_skeleton == on) return;
        _skeleton = on;
        if (!on)
        {
            Motion.Cancel(this, PulseChannel);
            Invalidate();
            return;
        }
        Motion.Loop(this, PulseChannel, PulseMs, phase =>
        {
            _pulse = PulseLow + (1 - PulseLow) * (0.5 + 0.5 * Math.Cos(2 * Math.PI * phase));
            Invalidate();
        });
    }

    private void ArrangeAll()
    {
        var width = Math.Max(0, ClientSize.Width);
        for (var i = 0; i < _rows.Count; i++) _rows[i].SetBounds(0, i * (RowHeight + RowGap), width, RowHeight);
        AutoScrollMinSize = new Size(0, _skeleton ? SkeletonRows * (RowHeight + RowGap) : _rows.Count * (RowHeight + RowGap) + BottomPad);
    }

    private void FadeOut(LobbyRow row) =>
        MotionOverlay.Play(row, Durations.Swap, Easing.Out, progress => new Layer((float)(1 - progress), 0, 0, 1, 0), Theme.Bg, done: () =>
        {
            Controls.Remove(row);
            row.Dispose();
        });

    /// <summary>One lobby: capsule, game, host, players, mod, ping and the action button, on a card that lightens on hover (150 ms)
    /// and snaps to the selection grey while pressed.</summary>
    private sealed class LobbyRow : Control
    {
        public static readonly int CapsuleWidth = Theme.Scale(30);
        public static readonly int CapsuleHeight = Theme.Scale(40);

        private const string HoverChannel = "hover";
        private static readonly int CapsuleGap = Theme.Scale(10);
        private static readonly int AvatarSize = Theme.Scale(24);
        private static readonly int AvatarGap = Theme.Scale(8);
        private static readonly int ButtonHeight = Theme.Scale(32);

        private readonly AvatarFade _avatar;
        private readonly SteamArt? _art;
        private readonly FlatButton _action = new("Join", ButtonKind.Ghost) { Size = new Size(ButtonWidth, ButtonHeight) };
        private double _hover;
        private bool _pressed;

        public LobbyRow(AvatarStore avatars, LobbyRowModel model, SteamArt? art)
        {
            _art = art;
            _avatar = new AvatarFade(this, avatars, "avatar");
            Cursor = Cursors.Hand;
            Controls.Add(_action);
            _action.Click += (_, _) => ActionClicked?.Invoke(Model);
            SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer
                     | ControlStyles.ResizeRedraw, true);
            Update(model);
        }

        public event Action<LobbyRowModel>? ActionClicked;
        public event Action<LobbyRowModel>? Opened;

        public LobbyRowModel Model { get; private set; } = null!;

        public void Update(LobbyRowModel model)
        {
            Model = model;
            _avatar.Show(model.Listing.HostId);
            Configure(_action, model.Action);
            Invalidate();
        }

        protected override void OnResize(EventArgs e)
        {
            _action.Location = new Point(Columns(Width, Height).Button, (Height - ButtonHeight) / 2);
            base.OnResize(e);
        }

        protected override void OnMouseEnter(EventArgs e)
        {
            SetHover(1);
            base.OnMouseEnter(e);
        }

        protected override void OnMouseLeave(EventArgs e)
        {
            if (!ClientRectangle.Contains(PointToClient(Cursor.Position))) SetHover(0);
            base.OnMouseLeave(e);
        }

        protected override void OnMouseDown(MouseEventArgs e)
        {
            _pressed = e.Button == MouseButtons.Left;
            Invalidate();
            base.OnMouseDown(e);
        }

        protected override void OnMouseUp(MouseEventArgs e)
        {
            _pressed = false;
            Invalidate();
            base.OnMouseUp(e);
        }

        protected override void OnMouseClick(MouseEventArgs e)
        {
            if (e.Button == MouseButtons.Left) Opened?.Invoke(Model);
            base.OnMouseClick(e);
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            var g = e.Graphics;
            g.Clear(_pressed ? Theme.Selection : Interpolate.Lerp(Theme.Card, Theme.CardHover, _hover));
            var columns = Columns(Width, Height);
            var listing = Model.Listing;
            var capsule = new Rectangle(columns.Game.X, (Height - CapsuleHeight) / 2, CapsuleWidth, CapsuleHeight);
            if (_art?.Capsule is { } image) Draw.Cover(g, image, capsule);
            else Draw.Fill(g, Theme.Selection, capsule);
            var nameX = capsule.Right + CapsuleGap;
            Draw.Text(g, Model.Game.Name, Theme.Body, Theme.Text, new Rectangle(nameX, 0, Math.Max(0, columns.Game.Right - nameX), Height));
            _avatar.Paint(g, new Rectangle(columns.Host.X, (Height - AvatarSize) / 2, AvatarSize, AvatarSize), listing.HostName, Theme.Card);
            var hostX = columns.Host.X + AvatarSize + AvatarGap;
            Draw.Text(g, listing.HostName, Theme.Body, Theme.Text, new Rectangle(hostX, 0, Math.Max(0, columns.Host.Right - hostX), Height));
            Draw.Text(g, LobbyRowModels.Players(listing), Theme.Body, Theme.Text, columns.Players);
            Draw.Text(g, listing.ModBuild > 0 ? listing.ModBuild.ToString() : string.Empty, Theme.MonoBody, Model.ModDiffers ? Theme.Problem : Theme.Text, columns.Mod);
            Draw.Text(g, LobbyRowModels.Ping(listing), Theme.MonoBody, Theme.Text, columns.Ping);
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing) _avatar.Dispose();
            base.Dispose(disposing);
        }

        private void SetHover(double target) =>
            Motion.Animate(this, HoverChannel, Motion.ValueOf(this, HoverChannel) ?? _hover, target, Durations.Hover, Easing.Css, value =>
            {
                _hover = value;
                Invalidate();
            });
    }
}
