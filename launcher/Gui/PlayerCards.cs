namespace CoopLauncher.Gui;

/// <summary>One seat of the Players section as the caller words it. Progress is the save transfer (0..1) while this seat
/// receives it, else null. An open seat shows the name and detail dimmed with an empty avatar outline.</summary>
public sealed record SeatCard(ulong AvatarId, string Name, string Role, string Detail, string Status, Color Lamp, string Ping,
    double? Progress, bool Open);

/// <summary>The Players section: one card per seat in two columns. A card has the Steam avatar (or the name's initial), the
/// name with the role on the right, the character, and the status with the ping on the right; a bar along the bottom edge
/// shows the save transfer. Motion: a seat that changes player cross-fades its whole content (skeleton-reveal), status
/// words swap, the ping rolls, the bar grows, and an avatar that arrives fades in over its placeholder. Lamps change at once.
/// For a game with a character select the cards are clickable: a card fades to the card hover fill, shows a right arrow
/// at its edge, and a click reports the character it shows.</summary>
internal sealed class PlayerCards : Control
{
    private const int Columns = 2;
    private const string PingSample = "9999 ms";
    private const string HoverSuffix = "hover";
    private const int NoSeat = -1;

    private static readonly int CardHeight = Theme.Scale(70);
    private static readonly int AvatarSize = Theme.Scale(48);
    private static readonly int AvatarX = Theme.Scale(12);
    private static readonly int AvatarY = Theme.Scale(11);
    private static readonly int TextX = Theme.Scale(72);
    private static readonly int PadRight = Theme.Scale(12);
    private static readonly int LineHeight = Theme.Scale(18);
    private static readonly int LineTop = Theme.Scale(8);
    private static readonly int LineMiddle = Theme.Scale(26);
    private static readonly int LineBottom = Theme.Scale(44);
    private static readonly int TwoLineTop = Theme.Scale(17);
    private static readonly int TwoLineBottom = Theme.Scale(35);
    private static readonly int LampGap = Theme.Scale(8);
    private static readonly int BarHeight = Theme.Scale(2);
    private static readonly int PingWidth = Draw.Width(PingSample, Theme.Mono);
    private static readonly int ArrowInset = Theme.Scale(12);
    private static readonly int ArrowColumn = Theme.ArrowSize / 2 + Theme.Gap;

    private readonly AvatarStore _avatars;
    private readonly List<Seat> _seats = new();
    private bool _clickable;
    private int _hoverSeat = NoSeat;

    private sealed class Seat
    {
        private readonly Control _owner;
        private readonly string _prefix;

        public Seat(PlayerCards owner, AvatarStore avatars, int index, SeatCard card)
        {
            _owner = owner;
            _prefix = $"seat{index}/";
            Card = card;
            Bar = card.Progress;
            Status = new StatusLine(owner, StatusChannel);
            Ping = new StatusLine(owner, PingChannel);
            Avatar = new AvatarFade(owner, avatars, _prefix + "avatar");
            Status.Snap(card.Status);
            Ping.Snap(card.Ping);
            Avatar.Show(card.AvatarId);
        }

        public SeatCard Card { get; set; }

        /// <summary>The content the seat showed before its player changed, while it fades out.</summary>
        public SeatCard? Previous { get; set; }

        /// <summary>Eased progress of the cross-fade from <see cref="Previous"/>.</summary>
        public double Reveal { get; set; }

        /// <summary>The save bar's shown share (0..1), or null for no bar.</summary>
        public double? Bar { get; set; }

        /// <summary>0 = at rest, 1 = hovered (a clickable card).</summary>
        public double Hover { get; set; }

        public StatusLine Status { get; }
        public StatusLine Ping { get; }
        public AvatarFade Avatar { get; }
        public string RevealChannel => _prefix + "reveal";
        public string BarChannel => _prefix + "bar";
        public string HoverChannel => _prefix + HoverSuffix;
        private string StatusChannel => _prefix + "status";
        private string PingChannel => _prefix + "ping";

        public void Dispose()
        {
            Avatar.Dispose();
            foreach (var channel in new[] { RevealChannel, BarChannel, HoverChannel, StatusChannel, PingChannel }) Motion.Cancel(_owner, channel);
        }
    }

    public PlayerCards(AvatarStore avatars)
    {
        _avatars = avatars;
        BackColor = Theme.Bg;
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer
                 | ControlStyles.ResizeRedraw, true);
    }

    /// <summary>Raised with the character a card shows (the card's detail line) when a clickable card is clicked.</summary>
    public event Action<string>? SeatClicked;

    /// <summary>The cards can be clicked: a hover fill, a right arrow, and <see cref="SeatClicked"/>.</summary>
    public bool Clickable
    {
        get => _clickable;
        set
        {
            if (_clickable == value) return;
            _clickable = value;
            SetHoverSeat(NoSeat);
            Cursor = Cursors.Default;
            Invalidate();
        }
    }

    /// <summary>The height of the control for this many seats.</summary>
    public static int HeightFor(int seats)
    {
        var rows = (seats + Columns - 1) / Columns;
        return rows == 0 ? 0 : rows * CardHeight + (rows - 1) * Theme.Gap;
    }

    /// <summary>Shows these seats. A seat keeps its card and moves only what changed; a seat whose player changed
    /// cross-fades.</summary>
    public void Show(IReadOnlyList<SeatCard> cards)
    {
        for (var i = 0; i < cards.Count; i++)
        {
            if (i == _seats.Count) _seats.Add(new Seat(this, _avatars, i, cards[i]));
            else Update(_seats[i], cards[i]);
        }
        while (_seats.Count > cards.Count)
        {
            _seats[^1].Dispose();
            _seats.RemoveAt(_seats.Count - 1);
        }
        Height = HeightFor(cards.Count);
        Invalidate();
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            foreach (var seat in _seats) seat.Dispose();
            _seats.Clear();
        }
        base.Dispose(disposing);
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;
        g.Clear(BackColor);
        for (var i = 0; i < _seats.Count; i++) PaintSeat(g, _seats[i], CardRect(i));
    }

    protected override void OnMouseMove(MouseEventArgs e)
    {
        SetHoverSeat(_clickable ? SeatAt(e.Location) : NoSeat);
        base.OnMouseMove(e);
    }

    protected override void OnMouseLeave(EventArgs e)
    {
        SetHoverSeat(NoSeat);
        base.OnMouseLeave(e);
    }

    protected override void OnMouseClick(MouseEventArgs e)
    {
        if (_clickable && SeatAt(e.Location) is var index and not NoSeat && _seats[index].Card.Detail.Length > 0)
            SeatClicked?.Invoke(_seats[index].Card.Detail);
        base.OnMouseClick(e);
    }

    private int SeatAt(Point point)
    {
        for (var i = 0; i < _seats.Count; i++)
            if (CardRect(i).Contains(point)) return i;
        return NoSeat;
    }

    private void SetHoverSeat(int index)
    {
        if (index == _hoverSeat) return;
        var previous = _hoverSeat;
        _hoverSeat = index;
        Cursor = index == NoSeat ? Cursors.Default : Cursors.Hand;
        if (previous != NoSeat && previous < _seats.Count) FadeHover(_seats[previous], 0);
        if (index != NoSeat) FadeHover(_seats[index], 1);
    }

    private void FadeHover(Seat seat, double target) =>
        Motion.Animate(this, seat.HoverChannel, Motion.ValueOf(this, seat.HoverChannel) ?? seat.Hover, target, Durations.Hover, Easing.Css, amount =>
        {
            seat.Hover = amount;
            Invalidate();
        });

    private Rectangle CardRect(int index)
    {
        var width = (Width - (Columns - 1) * Theme.Gap) / Columns;
        return new Rectangle(index % Columns * (width + Theme.Gap), index / Columns * (CardHeight + Theme.Gap), width, CardHeight);
    }

    private void Update(Seat seat, SeatCard card)
    {
        var old = seat.Card;
        seat.Card = card;
        if (old.AvatarId != card.AvatarId || old.Name != card.Name || old.Open != card.Open)
        {
            Replace(seat, old, card);
            return;
        }
        seat.Status.Set(card.Status);
        seat.Ping.Set(card.Ping);
        MoveBar(seat, card.Progress);
    }

    /// <summary>The seat's player changed: the old content fades out and blurs while the new content fades in.</summary>
    private void Replace(Seat seat, SeatCard old, SeatCard card)
    {
        seat.Previous = old;
        seat.Status.Snap(card.Status);
        seat.Ping.Snap(card.Ping);
        seat.Avatar.Show(card.AvatarId);
        Motion.Cancel(this, seat.BarChannel);
        seat.Bar = card.Progress;
        Motion.Animate(this, seat.RevealChannel, 0, 1, Durations.Reveal, Easing.InOut, progress =>
        {
            seat.Reveal = progress;
            Invalidate();
        }, () =>
        {
            seat.Previous = null;
            Invalidate();
        });
    }

    private void MoveBar(Seat seat, double? target)
    {
        if (target is not { } share)
        {
            Motion.Cancel(this, seat.BarChannel);
            seat.Bar = null;
            return;
        }
        var from = Motion.ValueOf(this, seat.BarChannel) ?? seat.Bar ?? 0;
        seat.Bar ??= 0;
        Motion.Animate(this, seat.BarChannel, from, share, Durations.Resize, Easing.Out, width =>
        {
            seat.Bar = width;
            Invalidate();
        });
    }

    private void PaintSeat(Graphics g, Seat seat, Rectangle rect)
    {
        if (seat.Previous is not { } previous)
        {
            PaintCard(g, rect, seat, seat.Card, live: true);
            return;
        }
        MotionLayer.Paint(g, rect, Recipes.RevealOut(seat.Reveal), FillOf(previous), (target, area) => PaintCard(target, area, seat, previous, live: false));
        MotionLayer.Paint(g, rect, Recipes.RevealIn(seat.Reveal), FillOf(seat.Card), (target, area) => PaintCard(target, area, seat, seat.Card, live: true));
    }

    private static Color FillOf(SeatCard card) => card.Open ? Theme.Bg : Theme.Bg2;

    /// <summary>One card. A live card uses the seat's animated status, ping and bar; a card that is fading out is drawn as
    /// it was.</summary>
    private void PaintCard(Graphics g, Rectangle rect, Seat seat, SeatCard card, bool live)
    {
        var hover = live && _clickable ? seat.Hover : 0;
        var fill = Interpolate.Lerp(FillOf(card), Theme.CardHover, hover);
        Draw.Fill(g, fill, rect);
        Draw.Border(g, rect, Theme.Line);
        var hasDetail = card.Detail.Length > 0;
        var arrow = live && _clickable && hasDetail;
        if (arrow)
            Draw.Arrow(g, Interpolate.Lerp(Theme.Muted, Theme.Text, hover), new Point(rect.Right - ArrowInset - Theme.ArrowSize / 4, rect.Y + rect.Height / 2), ArrowDirection.Right);

        var openInk = Interpolate.Lerp(Theme.Dim, Theme.Muted, hover);
        var avatar = new Rectangle(rect.X + AvatarX, rect.Y + AvatarY, AvatarSize, AvatarSize);
        if (card.Open) Draw.Border(g, avatar, Interpolate.Lerp(Theme.Line, Theme.Dim, hover));
        else if (live) seat.Avatar.Paint(g, avatar, card.Name, fill);
        else _avatars.Paint(g, avatar, card.AvatarId, card.Name);

        var top = rect.Y + (hasDetail ? LineTop : TwoLineTop);
        var bottom = rect.Y + (hasDetail ? LineBottom : TwoLineBottom);
        var left = rect.X + TextX;
        var right = rect.Right - PadRight - (arrow ? ArrowColumn : 0);
        var quiet = card.Open ? openInk : Theme.Muted;

        var roleWidth = Draw.Width(card.Role, Theme.Small);
        Draw.TextRight(g, card.Role, Theme.Small, quiet, new Rectangle(right - roleWidth, top, roleWidth, LineHeight));
        Draw.Text(g, card.Name, Theme.Strong, card.Open ? openInk : Theme.Text,
            new Rectangle(left, top, Math.Max(0, right - roleWidth - Theme.Gap - left), LineHeight));
        if (hasDetail) Draw.Text(g, card.Detail, Theme.Small, quiet, new Rectangle(left, rect.Y + LineMiddle, right - left, LineHeight));

        Draw.Lamp(g, card.Open ? Theme.Dim : card.Lamp, left, bottom + LineHeight / 2);
        if (card.Open) return;
        var statusX = left + Theme.LampSize + LampGap;
        var status = new Rectangle(statusX, bottom, Math.Max(0, right - PingWidth - Theme.Gap - statusX), LineHeight);
        var ping = new Rectangle(right - PingWidth, bottom, PingWidth, LineHeight);
        if (live)
        {
            seat.Status.Paint(g, status, Theme.Small, Theme.Text, fill);
            seat.Ping.Paint(g, ping, Theme.Mono, Theme.Muted, fill, alignRight: true);
        }
        else
        {
            Draw.Text(g, card.Status, Theme.Small, Theme.Text, status);
            Draw.TextRight(g, card.Ping, Theme.Mono, Theme.Muted, ping);
        }

        if ((live ? seat.Bar : card.Progress) is { } share && share > 0)
        {
            var inner = rect.Width - 2;
            Draw.Fill(g, Theme.Text, new Rectangle(rect.X + 1, rect.Bottom - 1 - BarHeight, (int)Math.Round(inner * Math.Clamp(share, 0, 1)), BarHeight));
        }
    }
}
