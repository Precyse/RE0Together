using System.Drawing.Drawing2D;

namespace CoopLauncher.Gui;

internal enum CardLayout { Wide, Tall, Mini, MiniPortrait }

/// <summary>One text of a card as it is painted: through its animated <see cref="StatusLine"/> while the card is live, as plain
/// text while the card is fading out.</summary>
internal readonly record struct CardText(StatusLine? Live, string Text)
{
    public string Shown => Live?.Text ?? Text;

    public void Paint(Graphics g, Rectangle bounds, Font font, Color color, Color background, bool alignRight = false)
    {
        if (Live != null) Live.Paint(g, bounds, font, color, background, alignRight);
        else if (alignRight) Draw.TextRight(g, Text, font, color, bounds);
        else Draw.Text(g, Text, font, color, bounds);
    }
}

/// <summary>Everything a card is painted from: the card, its three animated texts, the step list of a joining player, the save
/// bar's shown share, the spinner's phase and the state edge (whose waiting dots and portrait dimming the content takes).</summary>
internal sealed record CardView(PlayerCard Card, CardText Words, CardText Corner, CardText Detail, StepPainter? Steps, double? Bar,
    double SpinPhase, StateEdge Edge);

/// <summary>The four card layouts: where each part of a card goes and how it is painted. The card's chrome (fill, tint, edge,
/// glow) is the state edge's; this paints the content over it.</summary>
internal sealed class CardPainter
{
    public const int WideHeight = 140;
    public const int TallHeightTwo = 306;
    public const int TallHeightFour = 181;
    public const int MiniHeight = 38;
    public const int MiniPortraitHeight = 68;

    private const int TwoSeats = 2;
    private const string HostWord = "Host";
    private const int HoverOverlayAlpha = 18;

    private static readonly int WideGap = Theme.Scale(12);
    private static readonly int TallGapTwo = Theme.Scale(14);
    private static readonly int TallGapFour = Theme.Scale(10);
    private static readonly int MiniGap = Theme.Scale(6);
    private static readonly int MiniPortraitGap = Theme.Scale(8);

    private static readonly int WidePad = Theme.Scale(14);
    private static readonly Size WidePortrait = new(Theme.Scale(84), Theme.Scale(112));
    private static readonly int WidePortraitGap = Theme.Scale(16);
    private static readonly int WideLineGap = Theme.Scale(6);
    private static readonly int WideStepsMargin = Theme.Scale(2);

    private static readonly int TallPadTwo = Theme.Scale(12);
    private static readonly int TallPadFour = Theme.Scale(10);
    private static readonly int TallLineGapTwo = Theme.Scale(10);
    private static readonly int TallLineGapFour = Theme.Scale(8);
    private static readonly int TallBarGap = Theme.Scale(3);
    private static readonly int MinPortrait = Theme.Scale(40);

    private static readonly int MiniPadX = Theme.Scale(8);
    private static readonly int MiniPadY = Theme.Scale(6);
    private static readonly int MiniAvatar = Theme.Scale(26);
    private static readonly int MiniGapInside = Theme.Scale(8);
    private static readonly int MiniBarHeight = Theme.Scale(3);

    private static readonly int PortraitPad = Theme.Scale(8);
    private static readonly Size MiniPortraitBox = new(Theme.Scale(40), Theme.Scale(52));
    private static readonly int MiniPortraitTextGap = Theme.Scale(10);
    private static readonly int MiniPortraitLineGap = Theme.Scale(2);

    private static readonly int SpinGap = Theme.Scale(6);
    private static readonly int BarHeight = Theme.Scale(3);
    private static readonly int DotSize = Theme.Scale(6);
    private static readonly int DotGap = Theme.Scale(6);
    private static readonly int CornerGap = Theme.Scale(8);
    private const int DotCount = 3;

    private readonly AvatarStore _avatars;
    private readonly OutfitArtStore _art;

    public CardPainter(AvatarStore avatars, OutfitArtStore art)
    {
        _avatars = avatars;
        _art = art;
    }

    /// <summary>The gap between cards of the layout.</summary>
    public static int GapOf(CardLayout layout, int seats) => layout switch
    {
        CardLayout.Wide => WideGap,
        CardLayout.Tall => seats <= TwoSeats ? TallGapTwo : TallGapFour,
        CardLayout.Mini => MiniGap,
        _ => MiniPortraitGap,
    };

    /// <summary>The columns the layout puts its cards in.</summary>
    public static int ColumnsOf(CardLayout layout) => layout == CardLayout.Wide ? 1 : 2;

    /// <summary>A card's height; the compact layouts' is the least (their cards stretch to the control's height).</summary>
    public static int CardHeight(CardLayout layout, int seats) => Theme.Scale(layout switch
    {
        CardLayout.Wide => WideHeight,
        CardLayout.Tall => seats <= TwoSeats ? TallHeightTwo : TallHeightFour,
        CardLayout.Mini => MiniHeight,
        _ => MiniPortraitHeight,
    });

    /// <summary>The hover fill laid over a clickable card.</summary>
    public static void PaintHover(Graphics g, Rectangle card, double hover)
    {
        if (hover > 0) Draw.Fill(g, Color.FromArgb((int)Math.Round(HoverOverlayAlpha * hover), Theme.Text), card);
    }

    public void Paint(Graphics g, CardLayout layout, int seats, Rectangle rect, CardView view, Color background)
    {
        switch (layout)
        {
            case CardLayout.Wide:
                PaintWide(g, rect, view, background);
                break;
            case CardLayout.Tall:
                PaintTall(g, rect, view, background, seats);
                break;
            case CardLayout.Mini:
                PaintMini(g, rect, view, background);
                break;
            default:
                PaintMiniPortrait(g, rect, view, background);
                break;
        }
    }

    // ---------------------------------------------------------------- wide

    /// <summary>Portrait at the left; at its right name and corner, the detail line, and the words or the step list, as one
    /// block centred on the portrait.</summary>
    private void PaintWide(Graphics g, Rectangle rect, CardView view, Color background)
    {
        var card = view.Card;
        var portrait = new Rectangle(rect.X + WidePad, rect.Y + (rect.Height - WidePortrait.Height) / 2, WidePortrait.Width, WidePortrait.Height);
        PaintPortrait(g, portrait, view, Theme.Initial, background);

        var left = portrait.Right + WidePortraitGap;
        var right = rect.Right - WidePad;
        var hasDetail = view.Detail.Shown.Length > 0;
        var steps = view.Steps is { } list && card.Steps.Count > 0 ? list : null;
        var hasWords = steps != null || view.Words.Shown.Length > 0;
        var bottomHeight = steps?.Height(right - left) + WideStepsMargin ?? Theme.StepSmall.Height;
        var height = Theme.Name.Height + (hasDetail ? WideLineGap + Theme.Body.Height : 0) + (hasWords ? WideLineGap + bottomHeight : 0);
        var y = portrait.Y + (portrait.Height - height) / 2;

        PaintNameLine(g, new Rectangle(left, y, right - left, Theme.Name.Height), view, Theme.Name, background);
        y += Theme.Name.Height;
        if (hasDetail)
        {
            y += WideLineGap;
            view.Detail.Paint(g, new Rectangle(left, y, right - left, Theme.Body.Height), Theme.Body, Theme.Muted, background);
            y += Theme.Body.Height;
        }
        if (!hasWords) return;
        y += WideLineGap;
        if (steps != null) steps.Paint(g, new Rectangle(left, y + WideStepsMargin, right - left, bottomHeight - WideStepsMargin));
        else PaintStateRow(g, new Rectangle(left, y, right - left, Theme.StepSmall.Height), view, background, withBar: false);
    }

    // ---------------------------------------------------------------- tall

    /// <summary>A tall portrait over the name and corner, the detail line and the state row (words, then the footer or the
    /// percent at the right), a thin bar under it while the save arrives. The portrait takes the height the text leaves.</summary>
    private void PaintTall(Graphics g, Rectangle rect, CardView view, Color background, int seats)
    {
        var two = seats <= TwoSeats;
        var pad = two ? TallPadTwo : TallPadFour;
        var gap = two ? TallLineGapTwo : TallLineGapFour;
        var nameFont = two ? Theme.NameTall : Theme.StepStrong;
        var inner = Rectangle.Inflate(rect, -pad, -pad);
        var hasDetail = view.Detail.Shown.Length > 0;
        var steps = view.Steps is { } list && view.Card.Steps.Count > 0 ? list : null;
        var stateHeight = steps != null ? steps.Height(inner.Width) : Theme.StepSmall.Height;
        var textHeight = nameFont.Height + (hasDetail ? gap + Theme.StepSmall.Height : 0) + gap + stateHeight;
        var portrait = new Rectangle(inner.X, inner.Y, inner.Width, Math.Max(MinPortrait, inner.Height - gap - textHeight));
        PaintPortrait(g, portrait, view, two ? Theme.InitialLarge : Theme.Initial, background);

        var y = portrait.Bottom + gap;
        PaintNameLine(g, new Rectangle(inner.X, y, inner.Width, nameFont.Height), view, nameFont, background);
        y += nameFont.Height;
        if (hasDetail)
        {
            y += gap;
            view.Detail.Paint(g, new Rectangle(inner.X, y, inner.Width, Theme.StepSmall.Height), Theme.StepSmall, Theme.Muted, background);
            y += Theme.StepSmall.Height;
        }
        y += gap;
        if (steps != null)
        {
            steps.Paint(g, new Rectangle(inner.X, y, inner.Width, stateHeight));
            return;
        }
        var row = new Rectangle(inner.X, y, inner.Width, Theme.StepSmall.Height);
        PaintStateRow(g, row, view, background, withBar: true);
        if (view.Bar is { } share) PaintBar(g, new Rectangle(inner.X, row.Bottom + TallBarGap, inner.Width, BarHeight), share, Theme.Text);
    }

    // ---------------------------------------------------------------- mini

    /// <summary>A small avatar and the name; at the right the save percent (or the hold countdown) with the amber bar under it.</summary>
    private void PaintMini(Graphics g, Rectangle rect, CardView view, Color background)
    {
        var avatar = new Rectangle(rect.X + MiniPadX, rect.Y + (rect.Height - MiniAvatar) / 2, MiniAvatar, MiniAvatar);
        PaintPortrait(g, avatar, view, Theme.InitialSmall, background);
        var right = rect.Right - MiniPadX;
        var rightText = view.Card.RightText;
        var rightWidth = rightText.Length == 0 ? 0 : Draw.Width(rightText, Theme.Mono);
        var line = new Rectangle(avatar.Right + MiniGapInside, rect.Y, Math.Max(0, right - rightWidth - CornerGap - avatar.Right - MiniGapInside), rect.Height);
        Draw.Text(g, view.Card.Name, Theme.NameSmall, view.Card.State == CardState.Open ? Theme.Muted : Theme.Text, line);
        if (rightWidth > 0) Draw.TextRight(g, rightText, Theme.Mono, Theme.Muted, new Rectangle(right - rightWidth, rect.Y, rightWidth, rect.Height));
        if (view.Bar is { } share)
            PaintBar(g, new Rectangle(rect.X + MiniPadX, rect.Bottom - MiniPadY + 1, rect.Width - 2 * MiniPadX, MiniBarHeight), share, Theme.Armed);
    }

    /// <summary>A small portrait with the name, the character and the words stacked at its right.</summary>
    private void PaintMiniPortrait(Graphics g, Rectangle rect, CardView view, Color background)
    {
        var portrait = new Rectangle(rect.X + PortraitPad, rect.Y + (rect.Height - MiniPortraitBox.Height) / 2, MiniPortraitBox.Width, MiniPortraitBox.Height);
        PaintPortrait(g, portrait, view, Theme.Primary, background);
        var left = portrait.Right + MiniPortraitTextGap;
        var width = Math.Max(0, rect.Right - PortraitPad - left);
        var hasDetail = view.Detail.Shown.Length > 0;
        var hasWords = view.Words.Shown.Length > 0;
        var height = Theme.NameSmall.Height + (hasDetail ? MiniPortraitLineGap + Theme.Small.Height : 0) + (hasWords ? MiniPortraitLineGap + Theme.Small.Height : 0);
        var y = portrait.Y + (portrait.Height - height) / 2;
        Draw.Text(g, view.Card.Name, Theme.NameSmall, view.Card.State == CardState.Open ? Theme.Muted : Theme.Text, new Rectangle(left, y, width, Theme.NameSmall.Height));
        y += Theme.NameSmall.Height;
        if (hasDetail)
        {
            y += MiniPortraitLineGap;
            view.Detail.Paint(g, new Rectangle(left, y, width, Theme.Small.Height), Theme.Small, Theme.Muted, background);
            y += Theme.Small.Height;
        }
        if (hasWords) view.Words.Paint(g, new Rectangle(left, y + MiniPortraitLineGap, width, Theme.Small.Height), Theme.Small, Theme.Text, background);
    }

    // ---------------------------------------------------------------- parts

    /// <summary>The name at the left and the corner (you, the ping, the hold countdown) at the right, on one baseline.</summary>
    private static void PaintNameLine(Graphics g, Rectangle line, CardView view, Font nameFont, Color background)
    {
        var cornerFont = view.Corner.Shown.Any(char.IsDigit) ? Theme.Mono : Theme.Small;
        var cornerWidth = view.Corner.Shown.Length == 0 ? 0 : Math.Max(Draw.Width(view.Corner.Shown, cornerFont), Draw.Width(view.Card.Corner, cornerFont));
        var nameWidth = Math.Max(0, line.Width - cornerWidth - (cornerWidth > 0 ? CornerGap : 0));
        Draw.Text(g, view.Card.Name, nameFont, view.Card.State == CardState.Open ? Theme.Muted : Theme.Text, new Rectangle(line.X, line.Y, nameWidth, line.Height));
        if (cornerWidth == 0) return;
        var top = line.Y + Draw.Ascent(nameFont) - Draw.Ascent(cornerFont);
        view.Corner.Paint(g, new Rectangle(line.Right - cornerWidth, top, cornerWidth, cornerFont.Height), cornerFont, Theme.Muted, background, alignRight: true);
    }

    /// <summary>The state words (with the spinner in front while in progress) and at the right the percent or the footer.</summary>
    private static void PaintStateRow(Graphics g, Rectangle row, CardView view, Color background, bool withBar)
    {
        var left = row.X;
        if (view.Card.State == CardState.Progress)
        {
            var box = Spinner.BoxSize(SpinnerSize.Small);
            Spinner.Paint(g, new Rectangle(left, row.Y + (row.Height - box) / 2, box, box), view.SpinPhase, SpinnerSize.Small);
            left += box + SpinGap;
        }
        var right = row.Right;
        var percent = withBar && view.Bar != null;
        var rightText = percent ? view.Card.RightText : view.Card.Footer;
        if (rightText.Length > 0)
        {
            var font = percent ? Theme.MonoStep : Theme.Small;
            var width = Draw.Width(rightText, font);
            Draw.TextRight(g, rightText, font, percent ? Theme.Text : Theme.Muted, new Rectangle(right - width, row.Y, width, row.Height));
            right -= width + CornerGap;
        }
        view.Words.Paint(g, new Rectangle(left, row.Y, Math.Max(0, right - left), row.Height), Theme.StepSmall, Theme.Text, background);
    }

    private static void PaintBar(Graphics g, Rectangle track, double share, Color fill)
    {
        Draw.Fill(g, Theme.Line, track);
        Draw.Fill(g, fill, new Rectangle(track.X, track.Y, (int)Math.Round(track.Width * Math.Clamp(share, 0, 1)), track.Height));
    }

    /// <summary>The portrait box: the open seat's outline with its waiting dots, else the outfit cutout, the Steam avatar or the
    /// initial on the host's or the other players' gray; dimmed while the card has a problem.</summary>
    private void PaintPortrait(Graphics g, Rectangle box, CardView view, Font initialFont, Color background)
    {
        var card = view.Card;
        if (card.State == CardState.Open)
        {
            Draw.Border(g, box, Theme.Line);
            PaintWaitingDots(g, box, view.Edge);
            return;
        }
        var fill = card.Footer == HostWord || card.Corner == HostWord ? Theme.Raised : Theme.Selection;
        Draw.Fill(g, fill, box);
        if (card.Figure != null) _art.Paint(g, box, card.Figure, FigureBox.Portrait, fill);
        else if (_avatars.Get(card.AvatarId) is { } avatar) PaintAvatar(g, avatar, box);
        else Draw.Centered(g, Initials.Of(card.Name), initialFont, Theme.Muted, box);
        if (view.Edge.PortraitDim > 0) Draw.Fill(g, Color.FromArgb((int)Math.Round(255 * view.Edge.PortraitDim), Theme.Card), box);
    }

    private static void PaintAvatar(Graphics g, Bitmap avatar, Rectangle box)
    {
        var state = g.Save();
        g.InterpolationMode = InterpolationMode.HighQualityBicubic;
        g.PixelOffsetMode = PixelOffsetMode.Half;
        Draw.Cover(g, avatar, box);
        g.Restore(state);
    }

    private static void PaintWaitingDots(Graphics g, Rectangle box, StateEdge edge)
    {
        var span = DotCount * DotSize + (DotCount - 1) * DotGap;
        var x = box.X + (box.Width - span) / 2;
        var y = box.Y + (box.Height - DotSize) / 2;
        for (var i = 0; i < DotCount; i++)
            Draw.Fill(g, Interpolate.Lerp(Theme.Raised, Theme.Text, edge.DotLevel(i)), new Rectangle(x + i * (DotSize + DotGap), y, DotSize, DotSize));
    }
}
