namespace CoopLauncher.Gui;

/// <summary>A section of a pane: a caption, its content under it (no wider than Theme.ContentMaxWidth), a separator
/// under the section.</summary>
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

    public bool BottomLine { get; set; } = true;

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

    /// <summary>The caption, the stacked content and the bottom padding; a section without content takes no space.</summary>
    public int FittedHeight
    {
        get
        {
            var content = Controls.Cast<Control>().Sum(c => c.Height);
            return content == 0 ? 0 : Padding.Top + content + Theme.SectionPadY;
        }
    }

    public void FitToContent() => Height = FittedHeight;

    protected override void OnResize(EventArgs e)
    {
        var right = Math.Max(Theme.SectionPadX, Width - Theme.SectionPadX - Theme.ContentMaxWidth);
        if (Padding.Right != right) Padding = Padding with { Right = right };
        base.OnResize(e);
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        base.OnPaint(e);
        Draw.Caption(e.Graphics, _title, Theme.Muted, new Point(Theme.SectionPadX, Theme.SectionPadY));
        if (BottomLine) Draw.HorizontalLine(e.Graphics, 0, Width, Height - 1);
    }
}
