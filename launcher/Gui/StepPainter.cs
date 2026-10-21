namespace CoopLauncher.Gui;

internal enum StepMark { Pending, Active, Done, Failed }

internal enum StepSize { Small, Large }

/// <summary>One line of a step list. Trailing is a right-aligned number (percent, timer) that rolls its digits. Bar is a
/// share 0..1 under the text, or Indeterminate for a segment that travels along the track. Lines are mono lines under the text
/// (the file being received); Pairs are label and value rows (host build against yours).</summary>
internal sealed record StepRow(string Text, StepMark Mark, string Trailing = "", double? Bar = null, bool Indeterminate = false,
    IReadOnlyList<string>? Lines = null, IReadOnlyList<(string Label, string Value)>? Pairs = null);

/// <summary>Paints a list of steps on a control: a glyph (hollow circle, matrix spinner, green check, red cross), the text, a
/// trailing number, and under the text a bar, mono lines and label/value pairs. A row whose step becomes Done draws its check in
/// (success-check), one that fails fades in its cross and shakes once (error-state-shake), trailing numbers roll, bars tween.
/// The painter runs one <see cref="Motion.Loop"/> for every spinner and indeterminate bar it shows. Rows keep their place: a
/// list shown again updates the rows that are there and animates only what changed.</summary>
internal sealed class StepPainter : IDisposable
{
    private const double IndeterminateShare = 0.25;
    private const int CrossFadeMs = 150;
    private const int PairColumnGap = 16;
    private const int PairRowGap = 4;
    private const int CheckTotalMs = Durations.CheckDrawDelay + Durations.CheckDraw;

    private sealed class Metrics
    {
        public required int Glyph { get; init; }
        public required int Gap { get; init; }
        public required int Circle { get; init; }
        public required int Bar { get; init; }
        public required int SubGap { get; init; }
        public required int RowGap { get; init; }
        public required Font Text { get; init; }
        public required Font Strong { get; init; }
        public required Font Trailing { get; init; }
        public required SpinnerSize Spinner { get; init; }
    }

    private static readonly Metrics Large = new()
    {
        Glyph = Theme.Scale(18), Gap = Theme.Scale(12), Circle = Theme.Scale(10), Bar = Theme.Scale(4), SubGap = Theme.Scale(8),
        RowGap = Theme.Scale(14), Text = Theme.Step, Strong = Theme.StepStrong, Trailing = Theme.MonoBody, Spinner = SpinnerSize.Large,
    };

    private static readonly Metrics Small = new()
    {
        Glyph = Theme.Scale(12), Gap = Theme.Scale(8), Circle = Theme.Scale(8), Bar = Theme.Scale(3), SubGap = Theme.Scale(5),
        RowGap = Theme.Scale(5), Text = Theme.StepSmall, Strong = Theme.StepSmallStrong, Trailing = Theme.MonoStep, Spinner = SpinnerSize.Small,
    };

    private sealed class RowState
    {
        public RowState(StepPainter painter, int index, StepRow row)
        {
            var prefix = $"{painter._prefix}/{index}";
            Row = row;
            Text = new StatusLine(painter._owner, prefix + "/text");
            Trailing = new StatusLine(painter._owner, prefix + "/trailing");
            Text.Snap(row.Text);
            Trailing.Snap(row.Trailing);
            Bar = row.Bar;
            CheckMs = row.Mark == StepMark.Done ? CheckTotalMs : 0;
            CrossFade = row.Mark == StepMark.Failed ? 1 : 0;
            CheckChannel = prefix + "/check";
            CrossChannel = prefix + "/cross";
            ShakeChannel = prefix + "/shake";
            BarChannel = prefix + "/bar";
        }

        public StepRow Row { get; set; }
        public StatusLine Text { get; }
        public StatusLine Trailing { get; }

        /// <summary>The bar's shown share (0..1) while it tweens to the row's, or null for no bar.</summary>
        public double? Bar { get; set; }

        /// <summary>Milliseconds into the check's draw-in; the full time when it rests.</summary>
        public double CheckMs { get; set; }

        /// <summary>The cross's opacity (0..1).</summary>
        public double CrossFade { get; set; }

        /// <summary>The row's shake offset in 96 dpi pixels.</summary>
        public double ShakeX { get; set; }

        public string CheckChannel { get; }
        public string CrossChannel { get; }
        public string ShakeChannel { get; }
        public string BarChannel { get; }
    }

    private readonly Control _owner;
    private readonly string _prefix;
    private readonly Metrics _metrics;
    private readonly List<RowState> _rows = new();
    private int? _rowGap;
    private double _phase;
    private bool _looping;

    public StepPainter(Control owner, string prefix, StepSize size)
    {
        _owner = owner;
        _prefix = prefix;
        _metrics = size == StepSize.Large ? Large : Small;
    }

    /// <summary>The gap between rows in pixels; the size's own (14 large, 5 small) until set.</summary>
    public int RowGap
    {
        get => _rowGap ?? _metrics.RowGap;
        set => _rowGap = value;
    }

    /// <summary>The colour under the painted rows, which text animations are drawn over.</summary>
    public Color Background { get; set; } = Theme.Bg;

    /// <summary>The check's look at a time into its draw-in (success-check): opacity and rotation on the recipe's curve, the
    /// drop in 96 dpi pixels, and the share of the stroke drawn, which starts <see cref="Durations.CheckDrawDelay"/> late.</summary>
    public static (double Opacity, double Rotation, double Bob, double Drawn) CheckFrame(double ms)
    {
        var progress = Math.Clamp(ms / Durations.CheckDraw, 0, 1);
        var drawn = Math.Clamp((ms - Durations.CheckDrawDelay) / Durations.CheckDraw, 0, 1);
        return (Easing.Out.Ease(progress), Distances.CheckRotate * (1 - Easing.Out.Ease(progress)), Recipes.CheckBob(progress), Easing.Out.Ease(drawn));
    }

    /// <summary>Shows the rows. The first call shows them at once; later calls keep each row and animate what changed.</summary>
    public void Show(IReadOnlyList<StepRow> rows)
    {
        for (var i = 0; i < rows.Count; i++)
        {
            if (i == _rows.Count) _rows.Add(new RowState(this, i, rows[i]));
            else Update(_rows[i], rows[i]);
        }
        while (_rows.Count > rows.Count) Remove(_rows.Count - 1);
        SyncLoop();
        _owner.Invalidate();
    }

    /// <summary>The height of the rows with their gaps.</summary>
    public int Height(int width) => _rows.Sum(row => RowHeight(row.Row)) + Math.Max(0, _rows.Count - 1) * RowGap;

    public void Paint(Graphics g, Rectangle bounds)
    {
        var y = bounds.Y;
        foreach (var row in _rows)
        {
            var height = RowHeight(row.Row);
            var state = g.Save();
            g.TranslateTransform(Theme.ScaleF((float)row.ShakeX), 0);
            PaintRow(g, row, new Rectangle(bounds.X, y, bounds.Width, height));
            g.Restore(state);
            y += height + RowGap;
        }
    }

    public void Dispose()
    {
        for (var i = _rows.Count - 1; i >= 0; i--) Remove(i);
        Motion.Cancel(_owner, _prefix + "/loop");
    }

    private void Remove(int index)
    {
        var row = _rows[index];
        foreach (var channel in new[] { row.CheckChannel, row.CrossChannel, row.ShakeChannel, row.BarChannel }) Motion.Cancel(_owner, channel);
        row.Text.Snap(string.Empty);
        row.Trailing.Snap(string.Empty);
        _rows.RemoveAt(index);
    }

    private void Update(RowState state, StepRow row)
    {
        var before = state.Row.Mark;
        state.Row = row;
        state.Text.Set(row.Text);
        state.Trailing.Set(row.Trailing);
        MoveBar(state, row.Bar);
        if (before == row.Mark) return;
        if (row.Mark == StepMark.Done) DrawCheck(state);
        else state.CheckMs = 0;
        if (row.Mark == StepMark.Failed) Fail(state);
        else
        {
            Motion.Cancel(_owner, state.CrossChannel);
            state.CrossFade = 0;
        }
    }

    private void DrawCheck(RowState state)
    {
        state.CheckMs = 0;
        Motion.Animate(_owner, state.CheckChannel, 0, CheckTotalMs, CheckTotalMs, Easing.Linear, ms =>
        {
            state.CheckMs = ms;
            _owner.Invalidate();
        });
    }

    private void Fail(RowState state)
    {
        state.CrossFade = 0;
        Motion.Animate(_owner, state.CrossChannel, 0, 1, CrossFadeMs, Easing.Out, fade =>
        {
            state.CrossFade = fade;
            _owner.Invalidate();
        });
        Motion.Animate(_owner, state.ShakeChannel, 0, 1, Durations.ErrorShake, Easing.Linear, progress =>
        {
            state.ShakeX = Recipes.ErrorShakeX(progress);
            _owner.Invalidate();
        });
    }

    private void MoveBar(RowState state, double? target)
    {
        if (target is not { } share)
        {
            Motion.Cancel(_owner, state.BarChannel);
            state.Bar = null;
            return;
        }
        var from = Motion.ValueOf(_owner, state.BarChannel) ?? state.Bar ?? 0;
        state.Bar ??= 0;
        Motion.Animate(_owner, state.BarChannel, from, share, Durations.Resize, Easing.Out, shown =>
        {
            state.Bar = shown;
            _owner.Invalidate();
        });
    }

    /// <summary>Runs the loop while a spinner or an indeterminate bar shows.</summary>
    private void SyncLoop()
    {
        var needed = _rows.Any(row => row.Row.Mark == StepMark.Active || row.Row.Indeterminate);
        if (needed == _looping) return;
        _looping = needed;
        if (!needed)
        {
            Motion.Cancel(_owner, _prefix + "/loop");
            return;
        }
        Motion.Loop(_owner, _prefix + "/loop", Durations.Loop, phase =>
        {
            _phase = phase;
            _owner.Invalidate();
        });
    }

    private int TextHeight(StepRow row) => Math.Max(_metrics.Glyph, FontOf(row).Height);

    private Font FontOf(StepRow row) => row.Mark is StepMark.Active or StepMark.Failed ? _metrics.Strong : _metrics.Text;

    private bool HasBar(StepRow row) => row.Bar != null || row.Indeterminate;

    private int RowHeight(StepRow row)
    {
        var height = TextHeight(row);
        if (HasBar(row)) height += _metrics.SubGap + _metrics.Bar;
        height += (row.Lines?.Count ?? 0) * (_metrics.SubGap + Theme.Mono.Height);
        if (row.Pairs is { Count: > 0 } pairs) height += _metrics.SubGap + pairs.Count * Theme.MonoStep.Height + (pairs.Count - 1) * Theme.Scale(PairRowGap);
        return height;
    }

    private void PaintRow(Graphics g, RowState state, Rectangle rect)
    {
        var row = state.Row;
        var textHeight = TextHeight(row);
        var glyph = new Rectangle(rect.X, rect.Y + (textHeight - _metrics.Glyph) / 2, _metrics.Glyph, _metrics.Glyph);
        PaintGlyph(g, state, glyph);

        var font = FontOf(row);
        var color = row.Mark == StepMark.Pending ? Theme.Muted : Theme.Text;
        var left = rect.X + _metrics.Glyph + _metrics.Gap;
        var trailingWidth = row.Trailing.Length == 0 && state.Trailing.Text.Length == 0
            ? 0
            : Math.Max(Draw.Width(row.Trailing, _metrics.Trailing), Draw.Width(state.Trailing.Text, _metrics.Trailing));
        var trailingArea = new Rectangle(rect.Right - trailingWidth, rect.Y, trailingWidth, textHeight);
        var textArea = new Rectangle(left, rect.Y, Math.Max(0, trailingArea.X - Theme.Gap - left), textHeight);
        state.Text.Paint(g, textArea, font, color, Background);
        if (trailingWidth > 0) state.Trailing.Paint(g, trailingArea, _metrics.Trailing, Theme.Text, Background, alignRight: true);

        var y = rect.Y + textHeight;
        if (HasBar(row))
        {
            y += _metrics.SubGap;
            PaintBar(g, state, new Rectangle(left, y, Math.Max(0, rect.Right - left), _metrics.Bar));
            y += _metrics.Bar;
        }
        foreach (var line in row.Lines ?? [])
        {
            y += _metrics.SubGap;
            Draw.Text(g, line, Theme.Mono, Theme.Muted, new Rectangle(left, y, rect.Right - left, Theme.Mono.Height));
            y += Theme.Mono.Height;
        }
        if (row.Pairs is { Count: > 0 } pairs) PaintPairs(g, pairs, left, y + _metrics.SubGap, rect.Right);
    }

    private void PaintGlyph(Graphics g, RowState state, Rectangle glyph)
    {
        switch (state.Row.Mark)
        {
            case StepMark.Pending:
                Draw.HollowCircle(g, Theme.Dim, new Point(glyph.X + glyph.Width / 2, glyph.Y + glyph.Height / 2), _metrics.Circle);
                break;
            case StepMark.Active:
                Spinner.Paint(g, glyph, _phase, _metrics.Spinner);
                break;
            case StepMark.Done:
                PaintCheck(g, state.CheckMs, glyph);
                break;
            default:
                Draw.Cross(g, Color.FromArgb((int)Math.Round(255 * state.CrossFade), Theme.Problem), glyph);
                break;
        }
    }

    /// <summary>The check at a time into its draw-in: faded in, turned and dropped about the glyph's centre, its stroke drawn.</summary>
    private static void PaintCheck(Graphics g, double ms, Rectangle glyph)
    {
        var (opacity, rotation, bob, drawn) = CheckFrame(ms);
        var state = g.Save();
        g.TranslateTransform(glyph.X + glyph.Width / 2f, glyph.Y + glyph.Height / 2f + Theme.ScaleF((float)bob));
        g.RotateTransform((float)rotation);
        Draw.Check(g, Color.FromArgb((int)Math.Round(255 * opacity), Theme.Connected),
            new Rectangle(-glyph.Width / 2, -glyph.Height / 2, glyph.Width, glyph.Height), drawn);
        g.Restore(state);
    }

    private void PaintBar(Graphics g, RowState state, Rectangle track)
    {
        Draw.Fill(g, Theme.Line, track);
        if (state.Row.Bar != null)
        {
            Draw.Fill(g, Theme.Text, new Rectangle(track.X, track.Y, (int)Math.Round(track.Width * Math.Clamp(state.Bar ?? 0, 0, 1)), track.Height));
            return;
        }
        var width = (int)Math.Round(track.Width * IndeterminateShare);
        var x = track.X + (int)Math.Round(Interpolate.Lerp(-IndeterminateShare, 1, Easing.InOut.Ease(_phase)) * track.Width);
        var segment = Rectangle.Intersect(track, new Rectangle(x, track.Y, width, track.Height));
        if (segment.Width > 0) Draw.Fill(g, Theme.Text, segment);
    }

    private static void PaintPairs(Graphics g, IReadOnlyList<(string Label, string Value)> pairs, int left, int top, int right)
    {
        var labels = pairs.Max(pair => Draw.Width(pair.Label, Theme.MonoStep));
        var valueX = left + labels + Theme.Scale(PairColumnGap);
        var rowHeight = Theme.MonoStep.Height;
        for (var i = 0; i < pairs.Count; i++)
        {
            var y = top + i * (rowHeight + Theme.Scale(PairRowGap));
            Draw.Text(g, pairs[i].Label, Theme.MonoStep, Theme.Muted, new Rectangle(left, y, labels, rowHeight));
            Draw.Text(g, pairs[i].Value, Theme.MonoStep, Theme.Text, new Rectangle(valueX, y, Math.Max(0, right - valueX), rowHeight));
        }
    }
}
