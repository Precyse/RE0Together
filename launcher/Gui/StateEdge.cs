using System.Drawing.Drawing2D;

namespace CoopLauncher.Gui;

/// <summary>A card's chrome and its state animation: the gray fill with the state's tint over it, a 1 px edge, an inner glow of
/// four gradient bands, and per state an effect. InGame blooms once on entering and settles; Progress sends a comet around the
/// border and breathes; Problem pulses and shakes once on entering; Open is dashed and waits with three pulsing dots. Entering
/// a state cross-fades every colour and alpha over <see cref="Durations.Resize"/>. One instance per card; the owner is the
/// control that paints it and the prefix names its channels. With reduced motion everything is static (the glow at its rest
/// value, no comet, no pulse, no shake). A local card gets a 1 px outline outside the edge.</summary>
internal sealed class StateEdge
{
    private const string FadeChannel = "/fade";
    private const string BloomChannel = "/bloom";
    private const string ShakeChannel = "/shake";
    private const string LoopChannel = "/loop";

    private const int OpaqueAlpha = 255;
    private const int InGameEdgeAlpha = 153;
    private const int ProgressEdgeAlpha = 89;
    private const int ProblemEdgeAlpha = 204;
    private const int OpenFillAlpha = 110;

    private const double BloomPeak = 0.9;
    private const double BloomRest = 0.35;
    private const double ProgressGlowLow = 0.15;
    private const double ProgressGlowHigh = 0.35;
    private const double ProblemGlowLow = 0.25;
    private const double ProblemGlowHigh = 0.6;
    private const double ProgressGlowStatic = 0.25;
    private const double ProblemGlowStatic = 0.4;
    private const double CometShare = 0.18;
    private const int CometPieces = 16;
    private const int DotDelayMs = 400;
    private const double PortraitDimAmount = 0.5;

    private static readonly int GlowWidth = Theme.Scale(6);
    private static readonly int CompactGlowWidth = Theme.Scale(4);
    private static readonly int CometWidth = Theme.Scale(2);
    private static readonly int CompactCometWidth = Theme.Scale(1);
    private static readonly IReadOnlyList<Keyframes.Stop> _breathe = [new(0, 0), new(0.5, 1), new(1, 0)];

    /// <summary>Every number of a card's look that cross-fades between states.</summary>
    private readonly record struct Look(Color Fill, Color Tint, Color Edge, Color Hue, double Comet, double Dash, double Dim)
    {
        public static Look Lerp(Look from, Look to, double t) => new(
            Interpolate.Lerp(from.Fill, to.Fill, t), Interpolate.Lerp(from.Tint, to.Tint, t), Interpolate.Lerp(from.Edge, to.Edge, t),
            Interpolate.Lerp(from.Hue, to.Hue, t), Interpolate.Lerp(from.Comet, to.Comet, t), Interpolate.Lerp(from.Dash, to.Dash, t),
            Interpolate.Lerp(from.Dim, to.Dim, t));
    }

    private readonly Control _owner;
    private readonly string _prefix;
    private readonly Action _repaint;
    private Look _from;
    private Look _to;
    private CardState _state = CardState.Idle;
    private CardState _fromState = CardState.Idle;
    private bool _started;
    private bool _local;
    private double _fade = 1;
    private double _bloom = BloomRest;
    private double _phase;
    private double _shakeX;
    private int _loopPeriod;

    /// <summary>The owner paints the card and is invalidated (or <paramref name="repaint"/> runs, to invalidate less) when
    /// the look changes; the prefix names this card's channels.</summary>
    public StateEdge(Control owner, string channelPrefix, Action? repaint = null)
    {
        _owner = owner;
        _prefix = channelPrefix;
        _repaint = repaint ?? owner.Invalidate;
        _from = _to = LookOf(CardState.Idle, Theme.Text, Color.Transparent, reduced: false);
    }

    /// <summary>The compact layouts: a 4 px glow and a 1 px comet.</summary>
    public bool Compact { get; set; }

    public CardState State => _state;

    /// <summary>How far the card is shifted right (pixels, negative left) by its entry shake.</summary>
    public int ShakeOffset => (int)Math.Round(Theme.ScaleF((float)_shakeX));

    /// <summary>How much the portrait is dimmed (0..1): a card with a problem fades its portrait.</summary>
    public double PortraitDim => Current().Dim;

    /// <summary>The glow at the given moment of a state's animation (0..1 alpha at the edge). Pure: Progress breathes between 15
    /// and 35 %, Problem pulses between 25 and 60 %, InGame follows the bloom; reduced motion shows the rest values.</summary>
    public static double GlowLevel(CardState state, double phase, double bloom, bool reduced) => state switch
    {
        CardState.InGame => reduced ? BloomRest : bloom,
        CardState.Progress => reduced ? ProgressGlowStatic : Interpolate.Lerp(ProgressGlowLow, ProgressGlowHigh, Breathe(phase)),
        CardState.Problem => reduced ? ProblemGlowStatic : Interpolate.Lerp(ProblemGlowLow, ProblemGlowHigh, Breathe(phase)),
        _ => 0,
    };

    /// <summary>The breathing keyframes at a loop phase: 0 at the lap's start, 1 half way, 0 at its end, eased in and out.</summary>
    public static double Breathe(double phase) => Keyframes.At(_breathe, phase, Easing.InOut);

    /// <summary>The points of the border (a box's outline, clockwise from its top-left corner) between two distances along it
    /// from that corner; distances past one lap wrap around and negative ones count back from the corner.</summary>
    public static List<PointF> PerimeterPoints(RectangleF box, double from, double to)
    {
        var perimeter = 2 * (box.Width + box.Height);
        double[] corners = [0, box.Width, box.Width + box.Height, 2 * box.Width + box.Height];
        var points = new List<PointF> { PerimeterAt(box, from) };
        for (var lap = (int)Math.Floor(from / perimeter); lap <= (int)Math.Floor(to / perimeter); lap++)
            foreach (var corner in corners)
                if (lap * perimeter + corner is var distance && distance > from && distance < to) points.Add(PerimeterAt(box, distance));
        points.Add(PerimeterAt(box, to));
        return points;
    }

    /// <summary>Moves the card to a state: the look cross-fades and the state's entry effect starts. The first state a card gets
    /// is shown at once, without entry effects.</summary>
    public void Set(CardState state, bool local)
    {
        if (_local != local)
        {
            _local = local;
            _repaint();
        }
        if (_started && state == _state) return;

        var first = !_started;
        _started = true;
        var shownGlow = Glow();
        _from = Current();
        _to = LookOf(state, _to.Hue, _to.Tint, Motion.Reduced);
        _fromState = _state;
        _state = state;

        if (state == CardState.InGame) EnterInGame(first, shownGlow);
        else Motion.Cancel(_owner, _prefix + BloomChannel);
        if (state == CardState.Problem && !first && !Motion.Reduced) Shake();
        if (first || Motion.Reduced) Settle();
        else Fade();
        SyncLoop();
        _repaint();
    }

    /// <summary>Paints the card over the rectangle (the outer pixel is the local outline): fill, tint, glow, dashes or edge,
    /// comet. The owner paints the card's content after it.</summary>
    public void Paint(Graphics g, Rectangle card)
    {
        var look = Current();
        var inner = _local ? Rectangle.Inflate(card, -1, -1) : card;
        Draw.Fill(g, look.Fill, card);
        Draw.Fill(g, look.Tint, inner);
        PaintGlow(g, inner, Color.FromArgb((int)Math.Round(OpaqueAlpha * Math.Clamp(Glow(), 0, 1)), look.Hue));
        if (look.Edge.A > 0) Draw.Border(g, inner, look.Edge);
        if (look.Dash > 0) Draw.Dashed(g, inner, Color.FromArgb((int)Math.Round(OpaqueAlpha * look.Dash), Theme.Raised));
        if (look.Comet > 0 && !Motion.Reduced) PaintComet(g, inner, look);
        if (_local) Draw.Border(g, card, Theme.Text);
    }

    /// <summary>How lit an open seat's waiting dot (0, 1, 2) is: the matrix loader's keyframes, each dot 400 ms after the one
    /// before. With reduced motion the first dot is lit and the others rest.</summary>
    public double DotLevel(int dot)
    {
        if (Motion.Reduced) return dot == 0 ? 1 : 0;
        var own = _phase - (double)dot * DotDelayMs / Durations.Loop;
        return Spinner.Pulse(own - Math.Floor(own));
    }

    public void Dispose()
    {
        foreach (var channel in new[] { FadeChannel, BloomChannel, ShakeChannel, LoopChannel }) Motion.Cancel(_owner, _prefix + channel);
    }

    private static Look LookOf(CardState state, Color previousHue, Color previousTint, bool reduced)
    {
        var tint = CardStates.Tint(state);
        return new Look(
            Fill: state switch
            {
                CardState.Open => Color.FromArgb(OpenFillAlpha, Theme.Bg),
                CardState.Idle => Color.FromArgb(Theme.CardOverArtAlpha, Theme.Card),
                _ => Theme.Card,
            },
            Tint: tint.A == 0 ? Color.FromArgb(0, previousTint) : tint,
            Edge: EdgeOf(state, reduced),
            Hue: HueOf(state) ?? previousHue,
            Comet: state == CardState.Progress && !reduced ? 1 : 0,
            Dash: state == CardState.Open ? 1 : 0,
            Dim: state == CardState.Problem ? PortraitDimAmount : 0);
    }

    private static Color? HueOf(CardState state) => state switch
    {
        CardState.InGame => Theme.Connected,
        CardState.Progress => Theme.Armed,
        CardState.Problem => Theme.Problem,
        _ => null,
    };

    private static Color EdgeOf(CardState state, bool reduced)
    {
        if (state == CardState.Idle) return Theme.Line;
        if (state == CardState.Open) return Color.FromArgb(0, Theme.Line);
        var alpha = reduced ? OpaqueAlpha : state switch
        {
            CardState.InGame => InGameEdgeAlpha,
            CardState.Progress => ProgressEdgeAlpha,
            _ => ProblemEdgeAlpha,
        };
        return Color.FromArgb(alpha, HueOf(state)!.Value);
    }

    private Look Current() => Look.Lerp(_from, _to, _fade);

    private double Glow() => Interpolate.Lerp(
        GlowLevel(_fromState, _phase, _bloom, Motion.Reduced), GlowLevel(_state, _phase, _bloom, Motion.Reduced), _fade);

    private void Settle()
    {
        Motion.Cancel(_owner, _prefix + FadeChannel);
        _from = _to;
        _fromState = _state;
        _fade = 1;
    }

    private void Fade()
    {
        _fade = 0;
        Motion.Animate(_owner, _prefix + FadeChannel, 0, 1, Durations.Resize, Easing.Out, progress =>
        {
            _fade = progress;
            _repaint();
        }, () =>
        {
            _fromState = _state;
            SyncLoop();
        });
    }

    private void EnterInGame(bool first, double shownGlow)
    {
        if (first || Motion.Reduced)
        {
            _bloom = BloomRest;
            return;
        }
        var channel = _prefix + BloomChannel;
        _bloom = shownGlow;
        Motion.Animate(_owner, channel, shownGlow, BloomPeak, Durations.Bloom, Easing.Out, SetBloom, () =>
            Motion.Animate(_owner, channel, BloomPeak, BloomRest, Durations.BloomSettle, Easing.InOut, SetBloom));
    }

    private void SetBloom(double value)
    {
        _bloom = value;
        _repaint();
    }

    private void Shake() => Motion.Animate(_owner, _prefix + ShakeChannel, 0, 1, Durations.ErrorShake, Easing.Linear, progress =>
    {
        _shakeX = Recipes.ErrorShakeX(progress);
        _repaint();
    });

    /// <summary>Runs the one loop this card needs (Progress and Open on the matrix cycle, Problem on its pulse), also while a
    /// leaving state still cross-fades out; stops it when none is needed.</summary>
    private void SyncLoop()
    {
        var period = PeriodOf(_state) ?? (_fade < 1 ? PeriodOf(_fromState) : null);
        if (period is not { } lap)
        {
            Motion.Cancel(_owner, _prefix + LoopChannel);
            _loopPeriod = 0;
            return;
        }
        if (lap == _loopPeriod) return;
        _loopPeriod = lap;
        Motion.Loop(_owner, _prefix + LoopChannel, lap, phase =>
        {
            _phase = phase;
            _repaint();
        });
    }

    private static int? PeriodOf(CardState state) => state switch
    {
        CardState.Progress or CardState.Open => Durations.Loop,
        CardState.Problem => Durations.ProblemPulse,
        _ => null,
    };

    /// <summary>Four bands inside the edge, each a gradient from the glow colour at the edge to nothing inward.</summary>
    private void PaintGlow(Graphics g, Rectangle inner, Color glow)
    {
        if (glow.A == 0) return;
        var width = Compact ? CompactGlowWidth : GlowWidth;
        var area = Rectangle.Inflate(inner, -1, -1);
        if (area.Width <= 2 * width || area.Height <= 2 * width) return;
        var clear = Color.FromArgb(0, glow);
        Band(g, new Rectangle(area.X, area.Y, area.Width, width), glow, clear, LinearGradientMode.Vertical);
        Band(g, new Rectangle(area.X, area.Bottom - width, area.Width, width), clear, glow, LinearGradientMode.Vertical);
        Band(g, new Rectangle(area.X, area.Y, width, area.Height), glow, clear, LinearGradientMode.Horizontal);
        Band(g, new Rectangle(area.Right - width, area.Y, width, area.Height), clear, glow, LinearGradientMode.Horizontal);
    }

    private static void Band(Graphics g, Rectangle band, Color from, Color to, LinearGradientMode mode)
    {
        using var brush = new LinearGradientBrush(band, from, to, mode) { WrapMode = WrapMode.TileFlipXY };
        g.FillRectangle(brush, band);
    }

    /// <summary>The comet: the last 18 % of the border behind a head that makes one lap per loop, as 16 short strokes whose alpha
    /// rises from the tail to the head, each a path so that it turns the corners.</summary>
    private void PaintComet(Graphics g, Rectangle inner, Look look)
    {
        var width = Compact ? CompactCometWidth : CometWidth;
        var box = new RectangleF(inner.X + width / 2f, inner.Y + width / 2f, inner.Width - width, inner.Height - width);
        var perimeter = 2.0 * (box.Width + box.Height);
        var length = perimeter * CometShare;
        var tail = _phase * perimeter - length;
        var state = g.Save();
        g.SmoothingMode = SmoothingMode.AntiAlias;
        for (var piece = 0; piece < CometPieces; piece++)
        {
            var from = tail + length * piece / CometPieces;
            var alpha = (int)Math.Round(OpaqueAlpha * look.Comet * (piece + 0.5) / CometPieces);
            using var pen = new Pen(Color.FromArgb(alpha, look.Hue), width) { LineJoin = LineJoin.Miter };
            using var path = new GraphicsPath();
            path.AddLines(PerimeterPoints(box, from, from + length / CometPieces).ToArray());
            g.DrawPath(pen, path);
        }
        g.Restore(state);
    }

    private static PointF PerimeterAt(RectangleF box, double distance)
    {
        var perimeter = 2 * (box.Width + box.Height);
        var along = distance - Math.Floor(distance / perimeter) * perimeter;
        if (along <= box.Width) return new PointF((float)(box.X + along), box.Y);
        along -= box.Width;
        if (along <= box.Height) return new PointF(box.Right, (float)(box.Y + along));
        along -= box.Height;
        if (along <= box.Width) return new PointF((float)(box.Right - along), box.Bottom);
        along -= box.Width;
        return new PointF(box.X, (float)(box.Bottom - along));
    }
}
