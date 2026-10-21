namespace CoopLauncher.Gui;

/// <summary>A <see cref="StepPainter"/> as a control: it shows its rows on a transparent background and is as tall as they
/// are for its width. Shown over a screen's backdrop.</summary>
internal sealed class StepList : Control
{
    private readonly StepPainter _painter;

    public StepList(StepSize size)
    {
        _painter = new StepPainter(this, "steps", size);
        BackColor = Color.Transparent;
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer
                 | ControlStyles.ResizeRedraw | ControlStyles.SupportsTransparentBackColor, true);
    }

    /// <summary>The gap between rows in pixels (14 on a large list unless set, 12 while hosting).</summary>
    public int RowGap
    {
        get => _painter.RowGap;
        set
        {
            _painter.RowGap = value;
            Height = _painter.Height(Width);
            Invalidate();
        }
    }

    /// <summary>The colour the screen shows under the list, which text animations are drawn over.</summary>
    public Color Background
    {
        get => _painter.Background;
        set => _painter.Background = value;
    }

    public void Show(IReadOnlyList<StepRow> rows)
    {
        _painter.Show(rows);
        Height = _painter.Height(Width);
        Invalidate();
    }

    protected override void OnPaint(PaintEventArgs e) => _painter.Paint(e.Graphics, ClientRectangle);

    protected override void Dispose(bool disposing)
    {
        if (disposing) _painter.Dispose();
        base.Dispose(disposing);
    }
}
