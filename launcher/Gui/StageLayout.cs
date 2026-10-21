namespace CoopLauncher.Gui;

/// <summary>Where the parts of one half of the character select go: the figure box, the name row, the arrow row (previous
/// arrow, label, next arrow) and the dots row.</summary>
internal readonly record struct HalfLayout(Rectangle Figure, Rectangle Name, Rectangle Previous, Rectangle Label, Rectangle Next, Rectangle Dots)
{
    /// <summary>The arrow row as one rectangle.</summary>
    public Rectangle ArrowRow => Rectangle.Union(Previous, Next);
}

/// <summary>The geometry of the character select (S1, S3): two columns with the centre column between them, padded at the sides,
/// every half stacked from the bottom up (dots, arrow row, name row, figure) and centred in its column.</summary>
internal static class StageLayout
{
    private const int SidePad = 40;
    private const int CentreWidth = 72;
    private const int FigureWidth = 240;
    private const int FigureHeight = 300;
    private const int RowGap = 6;
    private const int ArrowGap = 8;
    private const int LabelWidth = 150;
    private const int DotSize = 6;
    private const int DotGap = 5;
    private const int BottomPad = 22;
    private const int SwapSize = 56;

    private static readonly int Pad = Theme.Scale(SidePad);
    private static readonly int Centre = Theme.Scale(CentreWidth);
    private static readonly int Gap = Theme.Scale(RowGap);

    /// <summary>The distance the slide of an outfit change reaches beyond the figure box, with room for its blur.</summary>
    public static readonly int SlideRoom = Theme.Scale((int)Distances.OutfitSlide + (int)Distances.PageBlur + 1);

    public static readonly int Dot = Theme.Scale(DotSize);
    public static readonly int DotSpacing = Theme.Scale(DotSize + DotGap);

    /// <summary>The columns of the two halves in a stage of this width.</summary>
    public static Rectangle[] Columns(Size size)
    {
        var half = (size.Width - 2 * Pad - Centre) / 2;
        return [new Rectangle(Pad, 0, half, size.Height), new Rectangle(Pad + half + Centre, 0, half, size.Height)];
    }

    /// <summary>The swap button's (or the lock's) square, centred in the centre column and the stage's height.</summary>
    public static Rectangle CentreBox(Size size)
    {
        var edge = Theme.Scale(SwapSize);
        return new Rectangle((size.Width - edge) / 2, (size.Height - edge) / 2, edge, edge);
    }

    public static HalfLayout Of(Rectangle column)
    {
        var arrow = Theme.Scale(StageArrow.Extent);
        var labelWidth = Theme.Scale(LabelWidth);
        var gap = Theme.Scale(ArrowGap);
        var dots = new Rectangle(0, column.Bottom - Theme.Scale(BottomPad) - Dot, column.Width, Dot);
        var arrows = new Rectangle(0, dots.Y - Gap - arrow, 2 * arrow + labelWidth + 2 * gap, arrow);
        arrows.X = column.X + (column.Width - arrows.Width) / 2;
        var name = new Rectangle(column.X, arrows.Y - Gap - Theme.Hero.Height, column.Width, Theme.Hero.Height);
        var figure = new Rectangle(column.X + (column.Width - Theme.Scale(FigureWidth)) / 2, Math.Max(0, name.Y - Gap - Theme.Scale(FigureHeight)),
            Theme.Scale(FigureWidth), Theme.Scale(FigureHeight));
        return new HalfLayout(
            figure, name, new Rectangle(arrows.X, arrows.Y, arrow, arrow), new Rectangle(arrows.X + arrow + gap, arrows.Y, labelWidth, arrow),
            new Rectangle(arrows.Right - arrow, arrows.Y, arrow, arrow), new Rectangle(column.X, dots.Y, column.Width, Dot));
    }
}
