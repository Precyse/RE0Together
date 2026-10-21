namespace CoopLauncher.Gui;

internal enum Side { You, Partner }

/// <summary>What one half of the stage says: the character's figure, its name, the role line (You, Partner, Open or a
/// player's name) and the outfit's label (empty when the outfit is not known).</summary>
internal sealed record StageHalf(Figure Figure, string Name, string Role, string Outfit);

/// <summary>The two halves of the character select: you on the left and your partner on the right, each with a portrait and
/// three lines of text (name, role, outfit); the swap button of the view sits in the column between them. The focused half
/// has the selection fill, which slides to the other half; a hovered half fades to the card hover fill. A portrait that
/// changes (a commit, a preview, a partner that joined) cross-fades, the two characters swapping places slide across, text
/// lines swap their words, and pictures that arrive late fade in. Left and Right move the focus.</summary>
internal sealed class CharacterStage : Control
{
    private const string FocusChannel = "focus";
    private const string SlideChannel = "slide";
    private const string ArrivalChannel = "arrival";
    private const int TextLines = 3;
    private const int OutfitMaxLines = 2;

    private static readonly int SwapColumn = Theme.Scale(52);
    private static readonly int PortraitWidth = Theme.Scale(146);
    private static readonly int NameGap = Theme.Scale(12);
    private static readonly int LineHeight = Theme.Scale(20);

    /// <summary>Single lines are centred in their line; a wrapped one starts at the top, so it is moved down to match.</summary>
    private static readonly int WrappedInset = (LineHeight - Theme.Body.Height) / 2;

    private readonly OutfitArtStore _art;
    private readonly Half?[] _halves = new Half?[2];
    private StageHalf[]? _model;
    private OutfitTile? _preview;
    private Side _focused = Side.You;
    private Side _focusFrom = Side.You;
    private double _focusSlide = 1;
    private double _slide = 1;
    private bool _sliding;
    private double? _arrival;

    private sealed class Half
    {
        public Half(CharacterStage owner, Side side, StageHalf model)
        {
            Figure = model.Figure;
            Name = new StatusLine(owner, $"{side}/name");
            Role = new StatusLine(owner, $"{side}/role");
            Outfit = new StatusLine(owner, $"{side}/outfit", wrapped: true);
            RevealChannel = $"{side}/reveal";
            HoverChannel = $"{side}/hover";
            Name.Snap(model.Name);
            Role.Snap(model.Role);
            Outfit.Snap(model.Outfit);
        }

        /// <summary>The figure at rest, or the one entering while <see cref="Previous"/> fades out.</summary>
        public Figure Figure { get; set; }

        public Figure? Previous { get; set; }

        /// <summary>Eased progress of the cross-fade from <see cref="Previous"/>.</summary>
        public double Reveal { get; set; }

        /// <summary>0 = at rest, 1 = hovered.</summary>
        public double Hover { get; set; }

        public StatusLine Name { get; }
        public StatusLine Role { get; }
        public StatusLine Outfit { get; }
        public string RevealChannel { get; }
        public string HoverChannel { get; }
    }

    public CharacterStage(OutfitArtStore art)
    {
        _art = art;
        BackColor = Theme.Bg;
        Height = StageHeight;
        TabStop = true;
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer
                 | ControlStyles.ResizeRedraw | ControlStyles.Selectable, true);
    }

    public static readonly int StageHeight = Theme.Scale(200);

    /// <summary>Raised when the player points at or moves to a half.</summary>
    public event Action<Side>? SideChosen;

    /// <summary>The half with the focus: its fill slides over from the other half.</summary>
    public Side FocusedSide
    {
        get => _focused;
        set
        {
            if (_focused == value) return;
            _focusFrom = _focused;
            _focused = value;
            _focusSlide = 0;
            if (_halves[(int)value] is { } half) SetHover(half, 0);
            Motion.Animate(this, FocusChannel, 0, 1, Durations.Tabs, Easing.Out, progress =>
            {
                _focusSlide = progress;
                Invalidate();
            });
        }
    }

    /// <summary>The column between the halves, which holds the view's swap button.</summary>
    public Rectangle SwapSlot => new(HalfWidth, 0, Width - 2 * HalfWidth, Height);

    private int HalfWidth => (Width - SwapColumn) / 2;

    /// <summary>Shows the two halves. The first call shows them at once; later, a half whose figure changed cross-fades, text
    /// lines swap their words, and two characters that traded places slide across.</summary>
    public void Show(StageHalf you, StageHalf partner)
    {
        var next = new[] { you, partner };
        if (_model == null)
        {
            _model = next;
            for (var i = 0; i < next.Length; i++) _halves[i] = new Half(this, (Side)i, Effective((Side)i));
            Invalidate();
            return;
        }
        var swapped = TradedPlaces(_model, next);
        _model = next;
        Refresh(Durations.Reveal, swapped);
    }

    /// <summary>Shows an outfit on the focused half until it is called again with null (the preview of a hovered tile).</summary>
    public void Preview(OutfitTile? tile)
    {
        if (_preview == tile) return;
        _preview = tile;
        Refresh(Durations.Swap, swapped: false);
    }

    /// <summary>Pictures arrived: they fade in over what showed in their place.</summary>
    public void ArtArrived() => Motion.Animate(this, ArrivalChannel, 0, 1, Durations.Reveal, Easing.Out, progress =>
    {
        _arrival = progress;
        Invalidate();
    }, () =>
    {
        _arrival = null;
        Invalidate();
    });

    protected override AccessibleObject CreateAccessibilityInstance() => new ItemListAccessibility(this,
        () => _model == null ? 0 : _model.Length, index => $"{_model![index].Name} {_model[index].Role}", index => HalfRect((Side)index),
        index => (Side)index == _focused, index => SideChosen?.Invoke((Side)index));

    protected override bool IsInputKey(Keys keyData) => keyData is Keys.Left or Keys.Right || base.IsInputKey(keyData);

    protected override void OnKeyDown(KeyEventArgs e)
    {
        if (e.KeyCode is Keys.Left or Keys.Right)
        {
            SideChosen?.Invoke(e.KeyCode == Keys.Left ? Side.You : Side.Partner);
            e.Handled = true;
        }
        base.OnKeyDown(e);
    }

    protected override void OnMouseMove(MouseEventArgs e)
    {
        for (var i = 0; i < _halves.Length; i++)
        {
            if (_halves[i] is not { } half) continue;
            var side = (Side)i;
            SetHover(half, side != _focused && HalfRect(side).Contains(e.Location) ? 1 : 0);
        }
        base.OnMouseMove(e);
    }

    protected override void OnMouseLeave(EventArgs e)
    {
        for (var i = 0; i < _halves.Length; i++)
            if (_halves[i] is { } half) SetHover(half, 0);
        base.OnMouseLeave(e);
    }

    protected override void OnMouseClick(MouseEventArgs e)
    {
        foreach (var side in Enum.GetValues<Side>())
            if (HalfRect(side).Contains(e.Location)) SideChosen?.Invoke(side);
        base.OnMouseClick(e);
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
        if (_model == null) return;
        foreach (var side in Enum.GetValues<Side>())
            Draw.Fill(g, Interpolate.Lerp(Theme.Bg, Theme.CardHover, _halves[(int)side]!.Hover), HalfRect(side));
        Draw.Fill(g, Theme.Selection, Interpolate.Lerp(HalfRect(_focusFrom), HalfRect(_focused), _focusSlide));
        foreach (var side in Enum.GetValues<Side>())
        {
            PaintPortrait(g, _halves[(int)side]!, side);
            PaintText(g, _halves[(int)side]!, side);
        }
        if (Focused && ShowFocusCues) Draw.Border(g, Rectangle.Inflate(HalfRect(_focused), -1, -1), Theme.Text);
    }

    private Rectangle HalfRect(Side side) => new(side == Side.You ? 0 : Width - HalfWidth, 0, HalfWidth, Height);

    private Rectangle PortraitRect(Side side) =>
        new(side == Side.You ? 0 : Width - PortraitWidth, 0, PortraitWidth, Height);

    private static bool TradedPlaces(StageHalf[] before, StageHalf[] after) =>
        before[0].Figure.Character != before[1].Figure.Character
        && before[0].Figure.Character == after[1].Figure.Character && before[1].Figure.Character == after[0].Figure.Character;

    /// <summary>The half as shown: the focused half shows the previewed outfit while there is one.</summary>
    private StageHalf Effective(Side side)
    {
        var half = _model![(int)side];
        return side == _focused && _preview != null ? half with { Figure = _preview.Figure, Outfit = _preview.Label } : half;
    }

    private void Refresh(int fadeMs, bool swapped)
    {
        foreach (var side in Enum.GetValues<Side>())
        {
            var half = _halves[(int)side]!;
            var shown = Effective(side);
            half.Name.Set(shown.Name);
            half.Role.Set(shown.Role);
            half.Outfit.Set(shown.Outfit);
            if (swapped) SnapFigure(half, shown.Figure);
            else ChangeFigure(half, shown.Figure, fadeMs);
        }
        if (swapped) Slide();
        Invalidate();
    }

    private void SnapFigure(Half half, Figure figure)
    {
        Motion.Cancel(this, half.RevealChannel);
        half.Previous = null;
        half.Figure = figure;
    }

    private void ChangeFigure(Half half, Figure figure, int fadeMs)
    {
        if (half.Figure == figure) return;
        half.Previous = half.Figure;
        half.Figure = figure;
        Motion.Animate(this, half.RevealChannel, 0, 1, fadeMs, Easing.Out, progress =>
        {
            half.Reveal = progress;
            Invalidate();
        }, () =>
        {
            half.Previous = null;
            Invalidate();
        });
    }

    private void Slide()
    {
        _sliding = true;
        _slide = 0;
        Motion.Animate(this, SlideChannel, 0, 1, Durations.Resize, Easing.InOut, progress =>
        {
            _slide = progress;
            Invalidate();
        }, () =>
        {
            _sliding = false;
            Invalidate();
        });
    }

    private void SetHover(Half half, double target)
    {
        if (Motion.ValueOf(this, half.HoverChannel) is null && half.Hover == target) return;
        Motion.Animate(this, half.HoverChannel, Motion.ValueOf(this, half.HoverChannel) ?? half.Hover, target, Durations.Hover, Easing.Css,
            amount =>
            {
                half.Hover = amount;
                Invalidate();
            });
    }

    /// <summary>The portrait at the half's edge; while two characters trade places, where it is between the two halves.</summary>
    private void PaintPortrait(Graphics g, Half half, Side side)
    {
        var rect = PortraitRect(side);
        if (_sliding) rect = Interpolate.Lerp(PortraitRect(Other(side)), rect, _slide);
        var background = side == _focused ? Theme.Selection : Theme.Bg;
        if (half.Previous is not { } previous)
        {
            _art.Paint(g, rect, half.Figure, FigureBox.Portrait, background, arrival: _sliding ? null : _arrival);
            return;
        }
        MotionLayer.Paint(g, rect, Recipes.RevealOut(half.Reveal), background,
            (target, area) => _art.Paint(target, area, previous, FigureBox.Portrait, background));
        MotionLayer.Paint(g, rect, Recipes.RevealIn(half.Reveal), background,
            (target, area) => _art.Paint(target, area, half.Figure, FigureBox.Portrait, background));
    }

    private static Side Other(Side side) => side == Side.You ? Side.Partner : Side.You;

    /// <summary>Name, role and outfit centred vertically beside the portrait: left aligned for you, right aligned for the
    /// partner, whose portrait is at the right edge.</summary>
    private void PaintText(Graphics g, Half half, Side side)
    {
        var halfRect = HalfRect(side);
        var portrait = PortraitRect(side);
        var alignRight = side == Side.Partner;
        var left = alignRight ? halfRect.X + NameGap : portrait.Right + NameGap;
        var right = alignRight ? portrait.X - NameGap : halfRect.Right - NameGap;
        var top = (Height - TextLines * LineHeight) / 2;
        var background = side == _focused ? Theme.Selection : Theme.Bg;
        var width = Math.Max(0, right - left);
        half.Name.Paint(g, new Rectangle(left, top, width, LineHeight), Theme.Brand, Theme.Text, background, alignRight);
        half.Role.Paint(g, new Rectangle(left, top + LineHeight, width, LineHeight), Theme.Body, Theme.Muted, background, alignRight);
        half.Outfit.Paint(g, new Rectangle(left, top + 2 * LineHeight + WrappedInset, width, OutfitMaxLines * LineHeight), Theme.Body, Theme.Text, background, alignRight);
    }
}
