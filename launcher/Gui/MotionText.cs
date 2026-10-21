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
/// its text over several lines (the bounds' height says how many) and only ever swaps words.</summary>
internal sealed class StatusLine
{
    private enum Phase { Rest, SwapOut, SwapIn, Digits }

    private readonly Control _owner;
    private readonly string _channel;
    private readonly bool _wrapped;
    private string _text = string.Empty;
    private string _previous = string.Empty;
    private Phase _phase = Phase.Rest;
    private double _progress;
    private bool _started;
    private HashSet<int> _changedRuns = new();

    public StatusLine(Control owner, string channel, bool wrapped = false)
    {
        _owner = owner;
        _channel = channel;
        _wrapped = wrapped;
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

        var kind = _wrapped ? TextChange.Kind.Words : TextChange.Classify(_text, text);
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
        else if (alignRight) Draw.TextRight(g, text, font, color, bounds);
        else Draw.Text(g, text, font, color, bounds);
    }
}
