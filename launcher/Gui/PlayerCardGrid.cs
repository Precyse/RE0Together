namespace CoopLauncher.Gui;

/// <summary>One seat as a card shows it. Corner is the text at the right of the name (You, the ping, the hold countdown), Detail
/// the line under it (character and outfit), Words the state line, Footer the text at the right of the state row (Host).
/// Steps is the mini step list of a joining player, Progress the save transfer (0..1) while this seat receives it, Countdown
/// the hold countdown of the compact layouts. An open seat is a card in the Open state. Figure is the outfit cutout, shown
/// instead of the Steam avatar.</summary>
internal sealed record PlayerCard(ulong AvatarId, string Name, string Corner, string Detail, string Words, string Footer,
    CardState State, bool Local, Figure? Figure, IReadOnlyList<StepRow> Steps, double? Progress, string Countdown);

/// <summary>The players of a session as cards in one of four layouts (see <see cref="CardLayout"/>): wide (two seats, one under
/// the other), tall (two or four seats, two columns), mini and mini portrait (the always-on-top window). Each card is painted by
/// its <see cref="StateEdge"/> (tint, edge, glow and the state's animation) and keeps its identity per seat: a seat whose player
/// changes cross-fades (skeleton-reveal), words swap, pings and percents roll, bars tween. <see cref="Enter"/> staggers every
/// card in (texts-reveal). A clickable grid fades a card's hover fill, shows the hand cursor and reports the seat clicked. The
/// control is transparent, so a screen's backdrop shows between and through the cards.</summary>
internal sealed class PlayerCardGrid : Control
{
    private const int NoSeat = -1;
    private const string HoverSuffix = "/hover";
    private const string SpinChannel = "spin";

    /// <summary>How far a shaking card strays from its place, so that its repaint covers the shake.</summary>
    private static readonly int ShakeBleed = Theme.Scale((int)Distances.ErrorShake);

    private readonly AvatarStore _avatars;
    private readonly OutfitArtStore _art;
    private readonly CardPainter _painter;
    private readonly List<Seat> _seats = new();
    private CardLayout _layout = CardLayout.Wide;
    private bool _clickable;
    private int _hoverSeat = NoSeat;
    private double _spinPhase;
    private bool _spinning;

    private sealed class Seat
    {
        private readonly Control _owner;
        private readonly string _prefix;

        public Seat(PlayerCardGrid owner, int index, PlayerCard card)
        {
            _owner = owner;
            _prefix = $"seat{index}";
            Card = card;
            Bar = card.Progress;
            Words = new StatusLine(owner, _prefix + "/words");
            Corner = new StatusLine(owner, _prefix + "/corner");
            Detail = new StatusLine(owner, _prefix + "/detail");
            Edge = new StateEdge(owner, _prefix, () => owner.Invalidate(owner.BleedRect(index)));
            Steps = new StepPainter(owner, _prefix + "/steps", StepSize.Small) { Background = Theme.Card };
            Words.Snap(card.Words);
            Corner.Snap(card.Corner);
            Detail.Snap(card.Detail);
            Edge.Set(card.State, card.Local);
            Steps.Show(card.Steps);
        }

        public PlayerCard Card { get; set; }

        /// <summary>The content the seat showed before its player changed, while it fades out.</summary>
        public PlayerCard? Previous { get; set; }

        /// <summary>Eased progress of the cross-fade from <see cref="Previous"/>.</summary>
        public double Reveal { get; set; }

        /// <summary>The save bar's shown share (0..1), or null for no bar.</summary>
        public double? Bar { get; set; }

        /// <summary>0 = at rest, 1 = hovered (a clickable grid).</summary>
        public double Hover { get; set; }

        public bool Entering { get; set; }
        public int EnterDelayMs { get; set; }
        public double EnterMs { get; set; }

        public StatusLine Words { get; }
        public StatusLine Corner { get; }
        public StatusLine Detail { get; }
        public StateEdge Edge { get; }
        public StepPainter Steps { get; private set; }

        public string RevealChannel => _prefix + "/reveal";
        public string BarChannel => _prefix + "/bar";
        public string HoverChannel => _prefix + HoverSuffix;
        public string EnterChannel => _prefix + "/enter";

        /// <summary>The step list starts over for a new player, so that none of the old player's rows animates.</summary>
        public void RestartSteps(IReadOnlyList<StepRow> rows)
        {
            Steps.Dispose();
            Steps = new StepPainter(_owner, _prefix + "/steps", StepSize.Small) { Background = Theme.Card };
            Steps.Show(rows);
        }

        public void Dispose()
        {
            Edge.Dispose();
            Steps.Dispose();
            foreach (var channel in new[] { RevealChannel, BarChannel, HoverChannel, EnterChannel, _prefix + "/words", _prefix + "/corner", _prefix + "/detail" })
                Motion.Cancel(_owner, channel);
        }
    }

    public PlayerCardGrid(AvatarStore avatars, OutfitArtStore art)
    {
        _avatars = avatars;
        _art = art;
        _painter = new CardPainter(avatars, art);
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer
                 | ControlStyles.ResizeRedraw | ControlStyles.SupportsTransparentBackColor, true);
        BackColor = Color.Transparent;
        avatars.Changed += OnPictureArrived;
        art.Changed += Invalidate;
    }

    /// <summary>Raised with the seat's index when a clickable card is clicked.</summary>
    public event Action<int>? SeatClicked;

    /// <summary>Hides <see cref="Control.Layout"/>, an event, as the contract names it.</summary>
    public new CardLayout Layout
    {
        get => _layout;
        set
        {
            if (_layout == value) return;
            _layout = value;
            foreach (var seat in _seats) seat.Edge.Compact = IsCompact;
            SyncSpin();
            Invalidate();
        }
    }

    /// <summary>The cards can be clicked: a hover fill, the hand cursor and <see cref="SeatClicked"/>.</summary>
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

    private bool IsCompact => _layout is CardLayout.Mini or CardLayout.MiniPortrait;

    /// <summary>The height of a grid of this many seats in the layout. The compact layouts' cards stretch to the height the
    /// control is given, so theirs is the least. Every layout's cards have a fixed height, whatever the width.</summary>
    public static int HeightFor(CardLayout layout, int seats, int width)
    {
        var rows = RowsOf(layout, seats);
        return rows == 0 ? 0 : rows * CardPainter.CardHeight(layout, seats) + (rows - 1) * CardPainter.GapOf(layout, seats);
    }

    /// <summary>Shows these seats. A seat keeps its card and moves only what changed; a seat whose player changed cross-fades;
    /// a seat that is new enters. The wide and tall layouts size the control to their cards.</summary>
    public void Show(IReadOnlyList<PlayerCard> cards)
    {
        var first = _seats.Count;
        for (var i = 0; i < cards.Count; i++)
        {
            if (i == _seats.Count) _seats.Add(NewSeat(i, cards[i]));
            else Update(_seats[i], cards[i]);
        }
        while (_seats.Count > cards.Count)
        {
            _seats[^1].Dispose();
            _seats.RemoveAt(_seats.Count - 1);
        }
        if (!IsCompact) Height = HeightFor(_layout, cards.Count, Width);
        if (first > 0) for (var i = first; i < _seats.Count; i++) EnterSeat(_seats[i], 0);
        SyncSpin();
        Invalidate();
    }

    /// <summary>Brings every card in, one after the other (texts-reveal).</summary>
    public new void Enter()
    {
        for (var i = 0; i < _seats.Count; i++) EnterSeat(_seats[i], i);
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            _avatars.Changed -= OnPictureArrived;
            _art.Changed -= Invalidate;
            foreach (var seat in _seats) seat.Dispose();
            _seats.Clear();
            Motion.Cancel(this, SpinChannel);
        }
        base.Dispose(disposing);
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        for (var i = 0; i < _seats.Count; i++)
        {
            var seat = _seats[i];
            var rect = CardRect(i);
            if (!e.ClipRectangle.IntersectsWith(BleedRect(i))) continue;
            var layer = EnterLayer(seat);
            MotionLayer.Paint(e.Graphics, rect, layer, Theme.Bg, (target, area) => PaintSeat(target, seat, area, layer != Layer.Rest));
        }
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
        if (_clickable && SeatAt(e.Location) is var index and not NoSeat) SeatClicked?.Invoke(index);
        base.OnMouseClick(e);
    }

    private static int RowsOf(CardLayout layout, int seats)
    {
        var columns = CardPainter.ColumnsOf(layout);
        return (seats + columns - 1) / columns;
    }

    private Rectangle CardRect(int index)
    {
        var columns = CardPainter.ColumnsOf(_layout);
        var gap = CardPainter.GapOf(_layout, _seats.Count);
        var rows = RowsOf(_layout, _seats.Count);
        var height = CardPainter.CardHeight(_layout, _seats.Count);
        if (IsCompact && rows > 0) height = Math.Max(height, (Height - (rows - 1) * gap) / rows);
        var width = (Width - (columns - 1) * gap) / columns;
        return new Rectangle(index % columns * (width + gap), index / columns * (height + gap), width, height);
    }

    /// <summary>The card's place with the room its shake needs on both sides.</summary>
    private Rectangle BleedRect(int index) => Rectangle.Inflate(CardRect(index), ShakeBleed, 0);

    private int SeatAt(Point point)
    {
        for (var i = 0; i < _seats.Count; i++)
            if (CardRect(i).Contains(point)) return i;
        return NoSeat;
    }

    private Seat NewSeat(int index, PlayerCard card) => new(this, index, card) { Edge = { Compact = IsCompact } };

    private void OnPictureArrived(ulong id)
    {
        if (_seats.Any(seat => seat.Card.AvatarId == id)) Invalidate();
    }

    // ---------------------------------------------------------------- motion

    private void SetHoverSeat(int index)
    {
        if (index == _hoverSeat) return;
        var previous = _hoverSeat;
        _hoverSeat = index;
        Cursor = index == NoSeat ? Cursors.Default : Cursors.Hand;
        if (previous != NoSeat && previous < _seats.Count) FadeHover(_seats[previous], 0, previous);
        if (index != NoSeat) FadeHover(_seats[index], 1, index);
    }

    private void FadeHover(Seat seat, double target, int index) =>
        Motion.Animate(this, seat.HoverChannel, Motion.ValueOf(this, seat.HoverChannel) ?? seat.Hover, target, Durations.Hover, Easing.Css, amount =>
        {
            seat.Hover = amount;
            Invalidate(CardRect(index));
        });

    /// <summary>Starts the seat's entry: it is held back by its place in the order, then rises, fades and unblurs.</summary>
    private void EnterSeat(Seat seat, int order)
    {
        var delay = order * Durations.Stagger;
        var total = delay + Durations.StaggerItem;
        seat.Entering = true;
        seat.EnterDelayMs = delay;
        seat.EnterMs = 0;
        Motion.Animate(this, seat.EnterChannel, 0, total, total, Easing.Linear, ms =>
        {
            seat.EnterMs = ms;
            Invalidate();
        }, () =>
        {
            seat.Entering = false;
            Invalidate();
        });
    }

    private static Layer EnterLayer(Seat seat) => seat.Entering
        ? Recipes.StaggerIn(Easing.Out.Ease(Math.Clamp((seat.EnterMs - seat.EnterDelayMs) / Durations.StaggerItem, 0, 1)))
        : Layer.Rest;

    private void Update(Seat seat, PlayerCard card)
    {
        var old = seat.Card;
        seat.Card = card;
        seat.Edge.Set(card.State, card.Local);
        if (old.AvatarId != card.AvatarId || old.Name != card.Name || (old.State == CardState.Open) != (card.State == CardState.Open))
        {
            Replace(seat, old, card);
            return;
        }
        seat.Words.Set(card.Words);
        seat.Corner.Set(card.Corner);
        seat.Detail.Set(card.Detail);
        seat.Steps.Show(card.Steps);
        MoveBar(seat, card.Progress);
    }

    /// <summary>The seat's player changed: the old content fades out and blurs while the new content fades in.</summary>
    private void Replace(Seat seat, PlayerCard old, PlayerCard card)
    {
        seat.Previous = old;
        seat.Words.Snap(card.Words);
        seat.Corner.Snap(card.Corner);
        seat.Detail.Snap(card.Detail);
        seat.RestartSteps(card.Steps);
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

    /// <summary>Runs the one loop that turns every spinner of a state row while one is on show.</summary>
    private void SyncSpin()
    {
        var needed = _layout is CardLayout.Wide or CardLayout.Tall
            && _seats.Any(seat => seat.Card.State == CardState.Progress && seat.Card.Steps.Count == 0);
        if (needed == _spinning) return;
        _spinning = needed;
        if (!needed)
        {
            Motion.Cancel(this, SpinChannel);
            return;
        }
        Motion.Loop(this, SpinChannel, Durations.Loop, phase =>
        {
            _spinPhase = phase;
            Invalidate();
        });
    }

    // ---------------------------------------------------------------- painting

    /// <summary>A card: while its player is changing, the old and the new content as two layers over the same chrome. A card that
    /// is entering as a layer paints plain texts (layers do not nest).</summary>
    private void PaintSeat(Graphics g, Seat seat, Rectangle rect, bool layered)
    {
        if (seat.Previous is not { } previous || layered)
        {
            PaintCard(g, seat, rect, seat.Card, live: !layered);
            return;
        }
        MotionLayer.Paint(g, rect, Recipes.RevealOut(seat.Reveal), Theme.Card, (target, area) => PaintCard(target, seat, area, previous, live: false));
        MotionLayer.Paint(g, rect, Recipes.RevealIn(seat.Reveal), Theme.Card, (target, area) => PaintCard(target, seat, area, seat.Card, live: false));
    }

    private void PaintCard(Graphics g, Seat seat, Rectangle rect, PlayerCard card, bool live)
    {
        var state = g.Save();
        g.TranslateTransform(seat.Edge.ShakeOffset, 0);
        seat.Edge.Paint(g, rect);
        CardPainter.PaintHover(g, rect, _clickable ? seat.Hover : 0);
        var steps = ReferenceEquals(card, seat.Card) ? seat.Steps : null;
        var view = new CardView(card, TextOf(seat.Words, card.Words, live), TextOf(seat.Corner, card.Corner, live), TextOf(seat.Detail, card.Detail, live),
            steps, live ? seat.Bar : card.Progress, _spinPhase, seat.Edge);
        seat.Steps.Background = seat.Edge.Surface;
        _painter.Paint(g, _layout, _seats.Count, rect, view, seat.Edge.Surface);
        g.Restore(state);
    }

    private static CardText TextOf(StatusLine line, string text, bool live) => live ? new CardText(line, text) : new CardText(null, text);
}
