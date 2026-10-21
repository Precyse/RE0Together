namespace CoopLauncher.Gui;

/// <summary>A section of a pane: a caption, its content under it (no wider than Theme.ContentMaxWidth), a separator
/// under the section.</summary>
internal sealed class SectionPanel : Panel
{
    private const string HeightChannel = "height";

    private static readonly int CaptionGap = Theme.Scale(26);

    private string _title;
    private bool _bottomLine = true;

    public SectionPanel(string title)
    {
        _title = title;
        BackColor = Theme.Bg;
        Padding = new Padding(Theme.SectionPadX, Theme.SectionPadY + CaptionGap, Theme.SectionPadX, 0);
        SetStyle(ControlStyles.ResizeRedraw | ControlStyles.OptimizedDoubleBuffer, true);
    }

    /// <summary>The separator under the section; the lowest section of a pane goes without one.</summary>
    public bool BottomLine
    {
        get => _bottomLine;
        set
        {
            if (_bottomLine == value) return;
            _bottomLine = value;
            Invalidate();
        }
    }

    /// <summary>Painted under the caption across the section's width (the hero art running on into the first section).</summary>
    public Action<Graphics, int>? Backdrop { get; set; }

    /// <summary>The band above the content that holds the caption.</summary>
    public int CaptionBand => Padding.Top;

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

    /// <summary>Sizes the section to its content: at once, or (animate) by tweening the height over card-resize. A section
    /// that is not on screen yet snaps either way.</summary>
    public void FitToContent(bool animate = false)
    {
        var target = FittedHeight;
        if (!animate)
        {
            Motion.Cancel(this, HeightChannel);
            Height = target;
            return;
        }
        Motion.Animate(this, HeightChannel, Motion.ValueOf(this, HeightChannel) ?? Height, target, Durations.Resize, Easing.Out,
            height => Height = (int)Math.Round(height));
    }

    protected override void OnResize(EventArgs e)
    {
        var right = Math.Max(Theme.SectionPadX, Width - Theme.SectionPadX - Theme.ContentMaxWidth);
        if (Padding.Right != right) Padding = Padding with { Right = right };
        base.OnResize(e);
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        base.OnPaint(e);
        Backdrop?.Invoke(e.Graphics, Width);
        Draw.Caption(e.Graphics, _title, Theme.Muted, new Point(Theme.SectionPadX, Theme.SectionPadY));
        if (BottomLine) Draw.HorizontalLine(e.Graphics, 0, Width, Height - 1);
    }
}
