namespace CoopLauncher.Gui;

/// <summary>A section of a pane: a caption, its content under it, a separator under the section.</summary>
internal sealed class SectionPanel : Panel
{
    private static readonly int CaptionGap = Theme.Scale(26);

    private string _title;

    public SectionPanel(string title)
    {
        _title = title;
        BackColor = Theme.Bg;
        Padding = new Padding(Theme.SectionPadX, Theme.SectionPadY + CaptionGap, Theme.SectionPadX, 0);
        SetStyle(ControlStyles.ResizeRedraw | ControlStyles.OptimizedDoubleBuffer, true);
    }

    public bool BottomLine { get; init; } = true;

    public string Title
    {
        get => _title;
        set
        {
            if (_title == value) return;
            _title = value;
            Invalidate();
        }
    }

    /// <summary>Sets the height to the caption, the stacked content and the bottom padding.</summary>
    public void FitToContent() => Height = Padding.Top + Controls.Cast<Control>().Sum(c => c.Height) + Theme.SectionPadY;

    protected override void OnPaint(PaintEventArgs e)
    {
        base.OnPaint(e);
        Draw.Caption(e.Graphics, _title, Theme.Muted, new Point(Theme.SectionPadX, Theme.SectionPadY));
        if (BottomLine) Draw.HorizontalLine(e.Graphics, 0, Width, Height - 1);
    }
}
