namespace CoopLauncher.Gui;

/// <summary>Which of the two looks a <see cref="SegmentTabs"/> has: the top bar's bare tabs, or the Lobbies filter inside a 1 px
/// border.</summary>
internal enum SegmentStyle { Tab, Filter }

/// <summary>A row of segments with one selected, shown by a <see cref="Theme.Selection"/> pill that slides to the segment that
/// is picked (tabs-sliding). Used for the top bar's Play / Lobbies tabs and the Lobbies filter, whose last segment can be
/// disabled (grey text, no hover, not focusable as a pick). Left and Right pick the neighbour when it has focus.</summary>
internal sealed class SegmentTabs : Control
{
    private const string PillChannel = "pill";
    private const int NoSegment = -1;

    private static readonly int TabHeight = Theme.Scale(30);
    private static readonly int FilterHeight = Theme.Scale(36);
    private static readonly int TabPad = Theme.Scale(14);
    private static readonly int FilterPad = Theme.Scale(16);

    private readonly SegmentStyle _style;
    private readonly string[] _labels;
    private readonly int[] _lefts;
    private readonly int[] _widths;
    private readonly int _disabled;
    private int _selected;
    private int _hover = NoSegment;
    private Rectangle _pill;

    public SegmentTabs(SegmentStyle style, IEnumerable<string> labels, int disabledIndex = NoSegment)
    {
        _style = style;
        _labels = labels.ToArray();
        _disabled = disabledIndex;
        var pad = style == SegmentStyle.Tab ? TabPad : FilterPad;
        _widths = _labels.Select(label => Draw.Width(label, Theme.Strong) + 2 * pad).ToArray();
        _lefts = new int[_labels.Length];
        for (var i = 1; i < _labels.Length; i++) _lefts[i] = _lefts[i - 1] + _widths[i - 1];
        Size = new Size(_widths.Sum(), style == SegmentStyle.Tab ? TabHeight : FilterHeight);
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer
                 | ControlStyles.ResizeRedraw | ControlStyles.Selectable | ControlStyles.SupportsTransparentBackColor, true);
        BackColor = Color.Transparent;
        Cursor = Cursors.Hand;
        TabStop = true;
        AccessibleRole = AccessibleRole.PageTabList;
        _pill = SegmentRect(0);
    }

    /// <summary>The player picked a segment (not raised when it is set from code).</summary>
    public event Action<int>? Picked;

    public int SelectedIndex
    {
        get => _selected;
        set => Select(value);
    }

    protected override bool IsInputKey(Keys keyData) => keyData is Keys.Left or Keys.Right || base.IsInputKey(keyData);

    protected override void OnKeyDown(KeyEventArgs e)
    {
        var step = e.KeyCode == Keys.Left ? -1 : e.KeyCode == Keys.Right ? 1 : 0;
        if (step != 0 && IsPickable(_selected + step))
        {
            Pick(_selected + step);
            e.Handled = true;
        }
        base.OnKeyDown(e);
    }

    protected override void OnMouseMove(MouseEventArgs e)
    {
        SetHover(SegmentAt(e.X));
        base.OnMouseMove(e);
    }

    protected override void OnMouseLeave(EventArgs e)
    {
        SetHover(NoSegment);
        base.OnMouseLeave(e);
    }

    protected override void OnMouseClick(MouseEventArgs e)
    {
        var segment = SegmentAt(e.X);
        if (IsPickable(segment)) Pick(segment);
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
        Draw.Fill(g, Theme.Selection, _pill);
        if (_style == SegmentStyle.Filter) Draw.Border(g, ClientRectangle, Theme.Line);
        for (var i = 0; i < _labels.Length; i++) Draw.Centered(g, _labels[i], Theme.Strong, InkOf(i), SegmentRect(i));
        if (Focused && ShowFocusCues) Draw.Border(g, Rectangle.Inflate(ClientRectangle, -1, -1), Theme.Text);
    }

    private Color InkOf(int index)
    {
        if (index == _disabled) return Theme.Disabled;
        return index == _selected || index == _hover ? Theme.Text : Theme.Muted;
    }

    private Rectangle SegmentRect(int index) => new(_lefts[index], 0, _widths[index], Height);

    private int SegmentAt(int x)
    {
        for (var i = 0; i < _labels.Length; i++)
            if (x >= _lefts[i] && x < _lefts[i] + _widths[i]) return i;
        return NoSegment;
    }

    private bool IsPickable(int index) => index >= 0 && index < _labels.Length && index != _disabled && index != _selected;

    private void Pick(int index)
    {
        Select(index);
        Picked?.Invoke(index);
    }

    private void SetHover(int index)
    {
        if (index == _disabled) index = NoSegment;
        if (_hover == index) return;
        _hover = index;
        Invalidate();
    }

    private void Select(int index)
    {
        if (index == _selected || index < 0 || index >= _labels.Length) return;
        _selected = index;
        var from = _pill;
        var to = SegmentRect(index);
        Motion.Animate(this, PillChannel, 0, 1, Durations.Tabs, Easing.Out, progress =>
        {
            _pill = Interpolate.Lerp(from, to, progress);
            Invalidate();
        });
    }
}
