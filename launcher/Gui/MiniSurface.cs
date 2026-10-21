namespace CoopLauncher.Gui;

/// <summary>The inside of the mini window: a header (the game's capsule and name, the state at the right, the expand button), a
/// body (the seat cards, or the one partner that was lost) and a footer (the newest event, Play solo, Leave). One control, so
/// that the window can animate it as a whole.</summary>
internal sealed class MiniSurface : Control
{
    private const string BarChannel = "bar";
    private const string SpinChannel = "spin";

    private static readonly int HeaderHeight = Theme.Scale(36);
    private static readonly int FooterHeight = Theme.Scale(44);
    private static readonly int PadX = Theme.Scale(12);
    private static readonly int PadY = Theme.Scale(10);
    private static readonly int Capsule = Theme.Scale(22);
    private static readonly int Gap = Theme.Scale(12);
    private static readonly int SmallGap = Theme.Scale(8);
    private static readonly Size AlertPortrait = new(Theme.Scale(52), Theme.Scale(64));
    private static readonly int BarHeight = Theme.Scale(3);
    private static readonly int AlertLineGap = Theme.Scale(6);
    private static readonly int ButtonHeight = Theme.Scale(30);
    private static readonly int ButtonPadding = Theme.Scale(28);
    private const float AlertPortraitOpacity = 0.6f;

    private readonly AvatarStore _avatars;
    private readonly OutfitArtStore _art;
    private readonly PlayerCardGrid _grid;
    private readonly ExpandButton _expand = new();
    private readonly FlatButton _playSolo = new("Play solo", ButtonKind.Ghost) { Height = ButtonHeight };
    private readonly FlatButton _leave = new("Leave", ButtonKind.Ghost) { Height = ButtonHeight };
    private readonly StatusLine _header;
    private readonly StatusLine _event;
    private readonly StatusLine _words;
    private readonly StatusLine _holding;
    private SteamArt? _steam;
    private MiniModel? _model;
    private double _bar;
    private double _spin;
    private bool _spinning;
    private bool _alerting;

    public MiniSurface(AvatarStore avatars, OutfitArtStore art)
    {
        _avatars = avatars;
        _art = art;
        _grid = new PlayerCardGrid(avatars, art);
        _header = new StatusLine(this, "header");
        _event = new StatusLine(this, "event");
        _words = new StatusLine(this, "words");
        _holding = new StatusLine(this, "holding");
        BackColor = Theme.Bg;
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
        _playSolo.Width = Draw.Width(_playSolo.Text, Theme.Strong) + ButtonPadding;
        _leave.Width = Draw.Width(_leave.Text, Theme.Strong) + ButtonPadding;
        Controls.AddRange([_grid, _expand, _playSolo, _leave]);
        _expand.Click += (_, _) => ExpandClicked?.Invoke();
        _leave.Click += (_, _) => LeaveClicked?.Invoke();
        _playSolo.Click += (_, _) => PlaySoloClicked?.Invoke();
    }

    public event Action? ExpandClicked;
    public event Action? LeaveClicked;
    public event Action? PlaySoloClicked;

    /// <summary>The y a drag by the header starts above.</summary>
    public int Header => HeaderHeight;

    public void Show(MiniModel model, SteamArt steam)
    {
        _model = model;
        _steam = steam;
        _header.Set(model.Header.Text);
        _event.Set(model.Event);
        _grid.Layout = model.Layout;
        _grid.Show(model.Seats);
        _grid.Visible = model.Alert == null;
        _playSolo.Visible = model.PlaySolo;
        ShowAlert(model.Alert);
        SyncSpin();
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
        if (_model == null) return;
        var g = e.Graphics;
        PaintHeader(g);
        Draw.HorizontalLine(g, 0, Width, HeaderHeight - 1);
        Draw.HorizontalLine(g, 0, Width, Height - FooterHeight);
        if (_model.Alert is { } alert) PaintAlert(g, alert);
        var footer = new Rectangle(PadX, Height - FooterHeight, Math.Max(0, FirstButtonLeft() - SmallGap - PadX), FooterHeight);
        _event.Paint(g, footer, Theme.Mono, Theme.Muted, Theme.Bg);
    }

    private int FirstButtonLeft() => _playSolo.Visible ? _playSolo.Left : _leave.Left;

    private void Arrange()
    {
        var body = new Rectangle(PadX, HeaderHeight + PadY, Math.Max(0, Width - 2 * PadX), Math.Max(0, Height - HeaderHeight - FooterHeight - 2 * PadY));
        _grid.Bounds = body;
        _expand.Location = new Point(Width - _expand.Width, 0);
        var y = Height - FooterHeight + (FooterHeight - ButtonHeight) / 2;
        _leave.Location = new Point(Width - PadX - _leave.Width, y);
        _playSolo.Location = new Point(_leave.Left - SmallGap - _playSolo.Width, y);
    }

    // ---------------------------------------------------------------- header

    private void PaintHeader(Graphics g)
    {
        var header = _model!.Header;
        var capsule = new Rectangle(PadX, (HeaderHeight - Capsule) / 2, Capsule, Capsule);
        if (_steam?.Capsule is { } image) Draw.Cover(g, image, capsule);
        else Draw.Fill(g, Theme.Raised, capsule);
        var right = _expand.Left - SmallGap;
        var font = header.Kind == MiniHeaderKind.Ping ? Theme.Mono : Theme.Label;
        var width = Math.Max(Draw.Width(_header.Text, font), Draw.Width(header.Text, font));
        var text = new Rectangle(right - width, 0, width, HeaderHeight);
        _header.Paint(g, text, font, Theme.Text, Theme.Bg, alignRight: true);
        var nameX = capsule.Right + Gap;
        var nameRight = text.X - SmallGap;
        if (header.Kind == MiniHeaderKind.Joining)
        {
            var box = Spinner.BoxSize(SpinnerSize.Small);
            nameRight -= box + SmallGap;
            Spinner.Paint(g, new Rectangle(text.X - SmallGap - box, (HeaderHeight - box) / 2, box, box), _spin, SpinnerSize.Small);
        }
        Draw.Text(g, _model.GameName, Theme.NameSmall, Theme.Text, new Rectangle(nameX, 0, Math.Max(0, nameRight - nameX), HeaderHeight));
    }

    // ---------------------------------------------------------------- the partner that was lost

    private void ShowAlert(MiniAlert? alert)
    {
        var appeared = alert != null && !_alerting;
        _alerting = alert != null;
        if (alert == null)
        {
            Motion.Cancel(this, BarChannel);
            return;
        }
        if (appeared)
        {
            Motion.Cancel(this, BarChannel);
            _words.Snap(alert.Words);
            _holding.Snap(alert.Holding);
            _bar = alert.Remaining ?? 0;
            return;
        }
        _words.Set(alert.Words);
        _holding.Set(alert.Holding);
        Motion.Animate(this, BarChannel, Motion.ValueOf(this, BarChannel) ?? _bar, alert.Remaining ?? 0, Durations.Resize, Easing.Out, value =>
        {
            _bar = value;
            Invalidate();
        });
    }

    private Rectangle BodyRect => new(0, HeaderHeight, Width, Height - HeaderHeight - FooterHeight);

    /// <summary>The partner alone: its portrait dimmed, the name, the spinner and words, the bar that shrinks with the hold and the
    /// line under it, as one block centred on the portrait.</summary>
    private void PaintAlert(Graphics g, MiniAlert alert)
    {
        var body = BodyRect;
        var portrait = new Rectangle(PadX + PadX, body.Y + (body.Height - AlertPortrait.Height) / 2, AlertPortrait.Width, AlertPortrait.Height);
        MotionLayer.Paint(g, portrait, new Layer(AlertPortraitOpacity, 0, 0, 1, 0), Theme.Bg, (target, area) => PaintPortrait(target, alert.Card, area));
        var left = portrait.Right + PadX + PadX;
        var width = Math.Max(0, body.Right - PadX - PadX - left);
        var spinner = Spinner.BoxSize(SpinnerSize.Small);
        var bar = alert.Remaining != null;
        var holding = alert.Holding.Length > 0 || _holding.Text.Length > 0;
        var height = Theme.NameTall.Height + AlertLineGap + Theme.StepSmall.Height + (bar ? AlertLineGap + BarHeight : 0) + (holding ? AlertLineGap + Theme.Mono.Height : 0);
        var y = portrait.Y + (portrait.Height - height) / 2;
        Draw.Text(g, alert.Card.Name, Theme.NameTall, Theme.Text, new Rectangle(left, y, width, Theme.NameTall.Height));
        y += Theme.NameTall.Height + AlertLineGap;
        Spinner.Paint(g, new Rectangle(left, y + (Theme.StepSmall.Height - spinner) / 2, spinner, spinner), _spin, SpinnerSize.Small);
        _words.Paint(g, new Rectangle(left + spinner + SmallGap, y, Math.Max(0, width - spinner - SmallGap), Theme.StepSmall.Height), Theme.StepSmall, Theme.Text, Theme.Bg);
        y += Theme.StepSmall.Height;
        if (bar)
        {
            y += AlertLineGap;
            Draw.Fill(g, Theme.Line, new Rectangle(left, y, width, BarHeight));
            Draw.Fill(g, Theme.Armed, new Rectangle(left, y, (int)Math.Round(width * Math.Clamp(_bar, 0, 1)), BarHeight));
            y += BarHeight;
        }
        if (holding) _holding.Paint(g, new Rectangle(left, y + AlertLineGap, width, Theme.Mono.Height), Theme.Mono, Theme.Muted, Theme.Bg);
    }

    private void PaintPortrait(Graphics g, PlayerCard card, Rectangle box)
    {
        Draw.Fill(g, Theme.Card, box);
        if (card.Figure != null) _art.Paint(g, box, card.Figure, FigureBox.Portrait, Theme.Card);
        else if (_avatars.Get(card.AvatarId) != null) _avatars.Paint(g, box, card.AvatarId, card.Name);
        else Draw.Centered(g, card.Name.Length > 0 ? char.ToUpperInvariant(card.Name[0]).ToString() : string.Empty, Theme.Initial, Theme.Muted, box);
    }

    /// <summary>Runs one loop for the spinners of the header and the alert while either shows.</summary>
    private void SyncSpin()
    {
        var needed = _model!.Header.Kind == MiniHeaderKind.Joining || _model.Alert != null;
        if (needed == _spinning) return;
        _spinning = needed;
        if (!needed)
        {
            Motion.Cancel(this, SpinChannel);
            return;
        }
        Motion.Loop(this, SpinChannel, Durations.Loop, phase =>
        {
            _spin = phase;
            Invalidate();
        });
    }
}
