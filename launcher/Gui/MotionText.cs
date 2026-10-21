using System.Text.RegularExpressions;

namespace CoopLauncher.Gui;

/// <summary>What changed between two texts of one status slot.</summary>
public static class TextChange
{
    public enum Kind { Same, NumbersOnly, Words }

    private const string NumberMarker = "\u0001";
    private static readonly Regex _digitRuns = new(@"\d+", RegexOptions.Compiled);

    /// <summary>Same when equal; NumbersOnly when the texts differ only in their digit runs ("Connected 42 ms" to
    /// "Connected 57 ms"); otherwise Words.</summary>
    public static Kind Classify(string before, string after)
    {
        if (before == after) return Kind.Same;
        return _digitRuns.Replace(before, NumberMarker) == _digitRuns.Replace(after, NumberMarker) ? Kind.NumbersOnly : Kind.Words;
    }

    internal static MatchCollection DigitRuns(string text) => _digitRuns.Matches(text);
}

/// <summary>One painted text slot whose text changes with the broadcast tool's motion: words swap (the old text leaves
/// upward, the new one enters from below, text-states-swap) and numbers roll (the words stay still, each changed digit
/// run re-enters from below, digit-roll-in). The owner calls <see cref="Paint"/> from its own paint. A wrapped slot draws
/// its text over several lines (the bounds' height says how many) and only ever swaps words, as does a centred one.</summary>
internal sealed class StatusLine
{
    private enum Phase { Rest, SwapOut, SwapIn, Digits }

    private readonly Control _owner;
    private readonly string _channel;
    private readonly bool _wrapped;
    private readonly bool _centered;
    private string _text = string.Empty;
    private string _previous = string.Empty;
    private Phase _phase = Phase.Rest;
    private double _progress;
    private bool _started;
    private HashSet<int> _changedRuns = new();

    public StatusLine(Control owner, string channel, bool wrapped = false, bool centered = false)
    {
        _owner = owner;
        _channel = channel;
        _wrapped = wrapped;
        _centered = centered;
    }

    public string Text => _text;

    /// <summary>Changes the text with motion. The first text a slot gets is shown at once.</summary>
    public void Set(string text)
    {
        if (!_started)
        {
            Snap(text);
            return;
        }
        if (text == _text) return;
        if (_phase == Phase.SwapOut)
        {
            _text = text;
            return;
        }

        var kind = _wrapped || _centered ? TextChange.Kind.Words : TextChange.Classify(_text, text);
        _previous = _text;
        _text = text;
        if (kind == TextChange.Kind.Words) StartSwap();
        else StartDigits();
    }

    /// <summary>Changes the text without motion (content that is entering as a whole).</summary>
    public void Snap(string text)
    {
        Motion.Cancel(_owner, _channel);
        _started = true;
        _text = text;
        _phase = Phase.Rest;
        _owner.Invalidate();
    }

    public void Paint(Graphics g, Rectangle bounds, Font font, Color color, Color background, bool alignRight = false)
    {
        switch (_phase)
        {
            case Phase.Rest:
                DrawText(g, _text, font, color, bounds, alignRight);
                break;
            case Phase.SwapOut:
                MotionLayer.Paint(g, bounds, Recipes.SwapOut(_progress), background,
                    (target, rect) => DrawText(target, _previous, font, color, rect, alignRight));
                break;
            case Phase.SwapIn:
                MotionLayer.Paint(g, bounds, Recipes.SwapIn(_progress), background,
                    (target, rect) => DrawText(target, _text, font, color, rect, alignRight));
                break;
            default:
                PaintDigits(g, bounds, font, color, background, alignRight);
                break;
        }
    }

    private void StartSwap()
    {
        _phase = Phase.SwapOut;
        Run(Durations.Swap, Easing.InOut, () =>
        {
            _phase = Phase.SwapIn;
            Run(Durations.Swap, Easing.InOut, () => _phase = Phase.Rest);
        });
    }

    private void StartDigits()
    {
        var before = TextChange.DigitRuns(_previous);
        var after = TextChange.DigitRuns(_text);
        _changedRuns = Enumerable.Range(0, after.Count).Where(i => before[i].Value != after[i].Value).ToHashSet();
        _phase = Phase.Digits;
        Run(Durations.Digit, Easing.Out, () => _phase = Phase.Rest);
    }

    private void Run(int durationMs, CubicBezier curve, Action done)
    {
        _progress = 0;
        Motion.Animate(_owner, _channel, 0, 1, durationMs, curve, progress =>
        {
            _progress = progress;
            _owner.Invalidate();
        }, done);
    }

    /// <summary>The text in pieces split at the digit runs, each piece placed where it rests in the whole text; the
    /// changed runs are drawn as layers.</summary>
    private void PaintDigits(Graphics g, Rectangle bounds, Font font, Color color, Color background, bool alignRight)
    {
        var layer = Recipes.DigitIn(_progress);
        var runs = TextChange.DigitRuns(_text);
        var start = 0;
        for (var i = 0; i <= runs.Count; i++)
        {
            var end = i < runs.Count ? runs[i].Index : _text.Length;
            PaintPiece(g, bounds, font, color, start, end, alignRight, background, null);
            if (i == runs.Count) break;
            PaintPiece(g, bounds, font, color, end, end + runs[i].Length, alignRight, background, _changedRuns.Contains(i) ? layer : null);
            start = end + runs[i].Length;
        }
    }

    private void PaintPiece(Graphics g, Rectangle bounds, Font font, Color color, int start, int end, bool alignRight,
        Color background, Layer? layer)
    {
        if (end <= start) return;
        var piece = _text[start..end];
        var edge = alignRight ? Draw.TightWidth(_text[end..], font) : Draw.TightWidth(_text[..start], font);
        var width = layer == null ? bounds.Width - edge : Draw.Width(piece, font);
        var x = alignRight ? bounds.Right - edge - width : bounds.X + edge;
        var rect = new Rectangle(x, bounds.Y, width, bounds.Height);
        if (layer is { } digits)
            MotionLayer.Paint(g, rect, digits, background, (target, area) => DrawText(target, piece, font, color, area, alignRight));
        else
            DrawText(g, piece, font, color, rect, alignRight);
    }

    private void DrawText(Graphics g, string text, Font font, Color color, Rectangle bounds, bool alignRight)
    {
        if (_wrapped) Draw.Wrapped(g, text, font, color, bounds, alignRight);
        else if (_centered) Draw.Centered(g, text, font, color, bounds);
        else if (alignRight) Draw.TextRight(g, text, font, color, bounds);
        else Draw.Text(g, text, font, color, bounds);
    }
}

/// <summary>One digit of a <see cref="ReelText"/> that spins: its position in the text, the digit it leaves and the one it lands
/// on, how many cells of its 0-9 strip pass by in between (a full spin and the distance), and the delay before it starts.</summary>
internal readonly record struct ReelColumn(int Index, int From, int To, int Cells, int DelayMs);

/// <summary>A small count that changes with the spinning-counter recipe: each changed digit is a clipped reel of 0-9 that makes
/// one full spin and lands on the new digit, the columns starting <see cref="Durations.ReelStagger"/> apart, a soft blur while a
/// reel moves. Texts that differ in anything but digits swap at once. Same Set, Snap and Paint shape as <see cref="StatusLine"/>.</summary>
internal sealed class ReelText
{
    private const int Digits = 10;
    private const int Spins = 1;
    private const int ColumnSlack = 2;

    private readonly Control _owner;
    private readonly string _channel;
    private string _text = string.Empty;
    private bool _started;
    private IReadOnlyList<ReelColumn> _columns = [];
    private double _ms;

    public ReelText(Control owner, string channel)
    {
        _owner = owner;
        _channel = channel;
    }

    public string Text => _text;

    /// <summary>The reels that turn when one text becomes another: the digits that differ at the same place of two texts that are
    /// otherwise equal, each starting a stagger after the one before. No reels when the texts differ in anything else.</summary>
    public static IReadOnlyList<ReelColumn> Plan(string before, string after)
    {
        if (before.Length != after.Length) return [];
        var columns = new List<ReelColumn>();
        for (var i = 0; i < after.Length; i++)
        {
            if (before[i] == after[i]) continue;
            if (!char.IsAsciiDigit(before[i]) || !char.IsAsciiDigit(after[i])) return [];
            var from = before[i] - '0';
            var to = after[i] - '0';
            columns.Add(new ReelColumn(i, from, to, Spins * Digits + (to - from + Digits) % Digits, columns.Count * Durations.ReelStagger));
        }
        return columns;
    }

    /// <summary>Changes the text; the first text a slot gets is shown at once.</summary>
    public void Set(string text)
    {
        if (!_started)
        {
            Snap(text);
            return;
        }
        if (text == _text) return;
        var plan = Plan(_text, text);
        if (plan.Count == 0)
        {
            Snap(text);
            return;
        }
        _text = text;
        _columns = plan;
        _ms = 0;
        var total = Durations.Reel + plan[^1].DelayMs;
        Motion.Animate(_owner, _channel, 0, total, total, Easing.Linear, ms =>
        {
            _ms = ms;
            _owner.Invalidate();
        }, () =>
        {
            _columns = [];
            _owner.Invalidate();
        });
    }

    /// <summary>Changes the text without motion.</summary>
    public void Snap(string text)
    {
        Motion.Cancel(_owner, _channel);
        _started = true;
        _text = text;
        _columns = [];
        _owner.Invalidate();
    }

    public void Paint(Graphics g, Rectangle bounds, Font font, Color color, Color background, bool alignRight = false)
    {
        var start = 0;
        foreach (var column in _columns)
        {
            PaintPiece(g, bounds, font, color, start, column.Index, alignRight);
            PaintColumn(g, bounds, font, color, background, column, alignRight);
            start = column.Index + 1;
        }
        PaintPiece(g, bounds, font, color, start, _text.Length, alignRight);
    }

    private int XOf(Rectangle bounds, Font font, int index, bool alignRight) =>
        alignRight ? bounds.Right - Draw.TightWidth(_text[index..], font) : bounds.X + Draw.TightWidth(_text[..index], font);

    private void PaintPiece(Graphics g, Rectangle bounds, Font font, Color color, int start, int end, bool alignRight)
    {
        if (end <= start) return;
        var piece = _text[start..end];
        var x = XOf(bounds, font, start, alignRight);
        Draw.Text(g, piece, font, color, new Rectangle(x, bounds.Y, Draw.Width(piece, font), bounds.Height));
    }

    private void PaintColumn(Graphics g, Rectangle bounds, Font font, Color color, Color background, ReelColumn column, bool alignRight)
    {
        var progress = Easing.Reel.Ease(Math.Clamp((_ms - column.DelayMs) / Durations.Reel, 0, 1));
        var width = Draw.TightWidth("0", font) + ColumnSlack;
        var rect = new Rectangle(XOf(bounds, font, column.Index, alignRight), bounds.Y, width, bounds.Height);
        var moving = progress is > 0 and < 1;
        var layer = moving ? new Layer(1, 0, 0, 1, Distances.ReelBlur * (float)(1 - progress)) : Layer.Rest;
        MotionLayer.Paint(g, rect, layer, background, (target, area) => PaintStrip(target, area, font, color, column, progress * column.Cells));
    }

    /// <summary>The reel's strip of digits, <paramref name="travelled"/> cells up from where it started, clipped to its cell.</summary>
    private static void PaintStrip(Graphics g, Rectangle area, Font font, Color color, ReelColumn column, double travelled)
    {
        var state = g.Save();
        g.SetClip(area, System.Drawing.Drawing2D.CombineMode.Intersect);
        var first = (int)Math.Floor(travelled);
        for (var cell = first; cell <= first + 1; cell++)
        {
            var y = area.Y + (int)Math.Round((cell - travelled) * area.Height);
            Draw.Text(g, ((column.From + cell) % Digits).ToString(), font, color, new Rectangle(area.X, y, area.Width, area.Height));
        }
        g.Restore(state);
    }
}
