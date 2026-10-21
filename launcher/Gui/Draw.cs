using System.Drawing.Drawing2D;
using System.Drawing.Imaging;

namespace CoopLauncher.Gui;

internal enum ArrowDirection { Up, Down, Left, Right }

/// <summary>Shared painting: text, lamps, separators and image placement.</summary>
internal static class Draw
{
    private const TextFormatFlags LeftMiddle = TextFormatFlags.Left | TextFormatFlags.VerticalCenter
        | TextFormatFlags.NoPrefix | TextFormatFlags.EndEllipsis | TextFormatFlags.SingleLine;
    private const TextFormatFlags RightMiddle = TextFormatFlags.Right | TextFormatFlags.VerticalCenter
        | TextFormatFlags.NoPrefix | TextFormatFlags.SingleLine;

    public static void Text(Graphics g, string text, Font font, Color color, Rectangle bounds) =>
        TextRenderer.DrawText(g, text, font, bounds, color, LeftMiddle);

    public static void TextRight(Graphics g, string text, Font font, Color color, Rectangle bounds) =>
        TextRenderer.DrawText(g, text, font, bounds, color, RightMiddle);

    public static void Centered(Graphics g, string text, Font font, Color color, Rectangle bounds) =>
        TextRenderer.DrawText(g, text, font, bounds, color,
            TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.NoPrefix | TextFormatFlags.SingleLine);

    /// <summary>A file path on one line, shortened in the middle when it does not fit.</summary>
    public static void Path(Graphics g, string path, Font font, Color color, Rectangle bounds) =>
        TextRenderer.DrawText(g, path, font, bounds, color,
            TextFormatFlags.Left | TextFormatFlags.VerticalCenter | TextFormatFlags.NoPrefix | TextFormatFlags.SingleLine | TextFormatFlags.PathEllipsis);

    public static int Width(string text, Font font) => TextRenderer.MeasureText(text, font).Width;

    /// <summary>The advance width of the text without the padding TextRenderer adds around it: where the next character
    /// of a longer string starts.</summary>
    public static int TightWidth(string text, Font font) =>
        TextRenderer.MeasureText(text, font, new Size(int.MaxValue, int.MaxValue),
            TextFormatFlags.NoPadding | TextFormatFlags.NoPrefix | TextFormatFlags.SingleLine).Width;

    private const TextFormatFlags Wrap = TextFormatFlags.Left | TextFormatFlags.Top | TextFormatFlags.WordBreak
        | TextFormatFlags.NoPrefix | TextFormatFlags.EndEllipsis;

    /// <summary>Text wrapped to the width, top-aligned in the bounds, left or right aligned.</summary>
    public static void Wrapped(Graphics g, string text, Font font, Color color, Rectangle bounds, bool alignRight = false) =>
        TextRenderer.DrawText(g, text, font, bounds, color, alignRight ? Wrap | TextFormatFlags.Right : Wrap);

    /// <summary>The height of the text wrapped to the width, capped at the given number of lines.</summary>
    public static int WrappedHeight(string text, Font font, int width, int maxLines) =>
        Math.Min(TextRenderer.MeasureText(text, font, new Size(width, int.MaxValue), Wrap).Height, font.Height * maxLines);

    /// <summary>The distance from the top of a text line to its baseline.</summary>
    public static int Ascent(Font font) =>
        (int)Math.Round((double)font.FontFamily.GetCellAscent(font.Style) / font.FontFamily.GetLineSpacing(font.Style) * font.Height);

    /// <summary>The width of the text with <paramref name="extra"/> pixels added after every glyph (letter spacing).</summary>
    public static int SpacedWidth(string text, Font font, int extra) => text.Sum(glyph => TightWidth(glyph.ToString(), font) + extra);

    /// <summary>Text with <paramref name="extra"/> pixels added after every glyph, starting at the left of the bounds and
    /// centred vertically in them.</summary>
    public static void TextSpaced(Graphics g, string text, Font font, Color color, Rectangle bounds, int extra)
    {
        var x = bounds.X;
        var y = bounds.Y + (bounds.Height - font.Height) / 2;
        foreach (var glyph in text)
        {
            var piece = glyph.ToString();
            TextRenderer.DrawText(g, piece, font, new Point(x, y), color, TextFormatFlags.NoPadding | TextFormatFlags.NoPrefix);
            x += TightWidth(piece, font) + extra;
        }
    }

    /// <summary>A section or status label: small bold caps.</summary>
    public static void Caption(Graphics g, string text, Color color, Point at) =>
        TextRenderer.DrawText(g, text.ToUpperInvariant(), Theme.Label, at, color, TextFormatFlags.NoPrefix);

    /// <summary>A state lamp: a solid square, never a dot.</summary>
    public static void Lamp(Graphics g, Color color, int x, int centerY)
    {
        using var brush = new SolidBrush(color);
        g.FillRectangle(brush, x, centerY - Theme.LampSize / 2, Theme.LampSize, Theme.LampSize);
    }

    /// <summary>A solid triangle Theme.ArrowSize across its base, pointing the given way, centred on the point.</summary>
    public static void Arrow(Graphics g, Color color, Point center, ArrowDirection direction)
    {
        var half = Theme.ArrowSize / 2;
        var tip = half / 2;
        var points = direction switch
        {
            ArrowDirection.Up => new[] { new Point(center.X - half, center.Y + tip), new Point(center.X + half, center.Y + tip), new Point(center.X, center.Y - tip) },
            ArrowDirection.Down => new[] { new Point(center.X - half, center.Y - tip), new Point(center.X + half, center.Y - tip), new Point(center.X, center.Y + tip) },
            ArrowDirection.Right => new[] { new Point(center.X - tip, center.Y - half), new Point(center.X - tip, center.Y + half), new Point(center.X + tip, center.Y) },
            _ => new[] { new Point(center.X + tip, center.Y - half), new Point(center.X + tip, center.Y + half), new Point(center.X - tip, center.Y) },
        };
        using var brush = new SolidBrush(color);
        g.FillPolygon(brush, points);
    }

    public static void HorizontalLine(Graphics g, int x1, int x2, int y)
    {
        using var pen = new Pen(Theme.Line);
        g.DrawLine(pen, x1, y, x2, y);
    }

    public static void VerticalLine(Graphics g, int x, int y1, int y2)
    {
        using var pen = new Pen(Theme.Line);
        g.DrawLine(pen, x, y1, x, y2);
    }

    public static void Fill(Graphics g, Color color, Rectangle bounds)
    {
        using var brush = new SolidBrush(color);
        g.FillRectangle(brush, bounds);
    }

    public static void Border(Graphics g, Rectangle bounds, Color color)
    {
        using var pen = new Pen(color);
        g.DrawRectangle(pen, bounds.X, bounds.Y, bounds.Width - 1, bounds.Height - 1);
    }

    /// <summary>Fills the rectangle with the image, cropping whatever overflows (CSS object-fit: cover), centred, or with
    /// <paramref name="anchorTop"/> keeping the top of the image (a figure's head and chest).</summary>
    public static void Cover(Graphics g, Image image, Rectangle dest, bool anchorTop = false) =>
        g.DrawImage(image, dest, CoverSource(image, dest, anchorTop), GraphicsUnit.Pixel);

    private static RectangleF CoverSource(Image image, Rectangle dest, bool anchorTop)
    {
        var scale = Math.Max((float)dest.Width / image.Width, (float)dest.Height / image.Height);
        var cropWidth = dest.Width / scale;
        var cropHeight = dest.Height / scale;
        return new RectangleF((image.Width - cropWidth) / 2, anchorTop ? 0 : (image.Height - cropHeight) / 2, cropWidth, cropHeight);
    }

    /// <summary><see cref="Cover"/> drawn at the given opacity (0..1).</summary>
    public static void Faded(Graphics g, Image image, Rectangle dest, float alpha, bool anchorTop = false)
    {
        var matrix = new ColorMatrix { Matrix33 = alpha };
        using var attributes = new ImageAttributes();
        attributes.SetColorMatrix(matrix);
        var source = CoverSource(image, dest, anchorTop);
        g.DrawImage(image, dest, source.X, source.Y, source.Width, source.Height, GraphicsUnit.Pixel, attributes);
    }

    /// <summary>A thin scroll position mark at the right edge of the track: nothing when the content fits, else a bar as
    /// tall as the visible share of the content, placed at the scroll offset.</summary>
    public static void ScrollThumb(Graphics g, Rectangle track, int contentHeight, int offset, int thumbWidth)
    {
        if (contentHeight <= track.Height) return;
        var height = Math.Max(thumbWidth, track.Height * track.Height / contentHeight);
        var y = track.Y + (track.Height - height) * offset / (contentHeight - track.Height);
        Fill(g, Theme.Line, new Rectangle(track.Right - thumbWidth, y, thumbWidth, height));
    }

    /// <summary>The image scaled to fit inside the box without cropping, anchored to the box's bottom-left corner.</summary>
    public static void FitBottomLeft(Graphics g, Image image, Rectangle box)
    {
        var scale = Math.Min((float)box.Width / image.Width, (float)box.Height / image.Height);
        var width = (int)(image.Width * scale);
        var height = (int)(image.Height * scale);
        g.DrawImage(image, new Rectangle(box.X, box.Bottom - height, width, height));
    }

    private const float GlyphBox = 18f;
    private const float GlyphStroke = 2.2f;
    private const float DashOn = 4f;
    private const float DashOff = 3f;
    private static readonly PointF[] CheckPath = { new(3, 9.5f), new(7.5f, 14), new(15, 4.5f) };

    /// <summary>The success check (path (3, 9.5) (7.5, 14) (15, 4.5) of an 18 unit box, round ends) in the box, drawn as far as
    /// <paramref name="drawn"/> (0..1) of its length.</summary>
    public static void Check(Graphics g, Color color, Rectangle box, double drawn)
    {
        var points = Partial(CheckPath, drawn).Select(point => GlyphPoint(box, point)).ToArray();
        if (points.Length < 2) return;
        using var pen = GlyphPen(color, box);
        var state = g.Save();
        g.SmoothingMode = SmoothingMode.AntiAlias;
        g.DrawLines(pen, points);
        g.Restore(state);
    }

    /// <summary>The first <paramref name="share"/> (0..1) of the polyline's length as a polyline.</summary>
    private static List<PointF> Partial(PointF[] path, double share)
    {
        var total = path.Zip(path.Skip(1), Distance).Sum();
        var left = Math.Clamp(share, 0, 1) * total;
        var drawn = new List<PointF> { path[0] };
        for (var i = 1; i < path.Length && left > 0; i++)
        {
            var length = Distance(path[i - 1], path[i]);
            var take = Math.Min(1, left / length);
            drawn.Add(new PointF((float)(path[i - 1].X + (path[i].X - path[i - 1].X) * take), (float)(path[i - 1].Y + (path[i].Y - path[i - 1].Y) * take)));
            left -= length;
        }
        return drawn;
    }

    private static double Distance(PointF from, PointF to) => Math.Sqrt((to.X - from.X) * (to.X - from.X) + (to.Y - from.Y) * (to.Y - from.Y));

    /// <summary>The failed cross, corner to corner of an 18 unit box inset 4 units.</summary>
    public static void Cross(Graphics g, Color color, Rectangle box)
    {
        using var pen = GlyphPen(color, box);
        var state = g.Save();
        g.SmoothingMode = SmoothingMode.AntiAlias;
        g.DrawLine(pen, GlyphPoint(box, new PointF(4, 4)), GlyphPoint(box, new PointF(14, 14)));
        g.DrawLine(pen, GlyphPoint(box, new PointF(14, 4)), GlyphPoint(box, new PointF(4, 14)));
        g.Restore(state);
    }

    /// <summary>The pending step's hollow circle: a 1 px ring of the given outer diameter.</summary>
    public static void HollowCircle(Graphics g, Color color, Point center, int diameter)
    {
        using var pen = new Pen(color);
        var state = g.Save();
        g.SmoothingMode = SmoothingMode.AntiAlias;
        g.DrawEllipse(pen, center.X - diameter / 2f, center.Y - diameter / 2f, diameter - 1, diameter - 1);
        g.Restore(state);
    }

    /// <summary>A 1 px dashed border (dash 4, gap 3) just inside the rectangle: an open seat.</summary>
    public static void Dashed(Graphics g, Rectangle rect, Color color)
    {
        using var pen = new Pen(color) { DashPattern = new[] { DashOn, DashOff } };
        g.DrawRectangle(pen, rect.X, rect.Y, rect.Width - 1, rect.Height - 1);
    }

    /// <summary>Art laid over a screen: covers the rectangle and is drawn at the opacity (0..1).</summary>
    public static void Backdrop(Graphics g, Image image, Rectangle rect, float opacity) => Faded(g, image, rect, opacity);

    /// <summary>A padlock (shackle over a body, 2 unit stroke) in the 18 by 22 unit box, scaled to the box's width.</summary>
    public static void Lock(Graphics g, Color color, Rectangle box)
    {
        var unit = box.Width / GlyphBox;
        PointF At(float x, float y) => new(box.X + x * unit, box.Y + y * unit);
        using var pen = new Pen(color, 2 * unit);
        using var shackle = new GraphicsPath();
        shackle.AddLine(At(5, 10), At(5, 6));
        shackle.AddArc(box.X + 5 * unit, box.Y + 2 * unit, 8 * unit, 8 * unit, 180, 180);
        shackle.AddLine(At(13, 6), At(13, 10));
        var state = g.Save();
        g.SmoothingMode = SmoothingMode.AntiAlias;
        g.DrawRectangle(pen, box.X + 2 * unit, box.Y + 10 * unit, 14 * unit, 11 * unit);
        g.DrawPath(pen, shackle);
        g.Restore(state);
    }

    /// <summary>The expand corners: two corner brackets of a 12 unit box, 1.4 unit stroke.</summary>
    public static void Expand(Graphics g, Color color, Rectangle box)
    {
        var unit = Math.Min(box.Width, box.Height) / 12f;
        PointF At(float x, float y) => new(box.X + x * unit, box.Y + y * unit);
        using var pen = new Pen(color, 1.4f * unit);
        var state = g.Save();
        g.SmoothingMode = SmoothingMode.AntiAlias;
        g.DrawLines(pen, new[] { At(1, 5), At(1, 1), At(5, 1) });
        g.DrawLines(pen, new[] { At(7, 11), At(11, 11), At(11, 7) });
        g.Restore(state);
    }

    private static PointF GlyphPoint(Rectangle box, PointF unit)
    {
        var scale = Math.Min(box.Width, box.Height) / GlyphBox;
        return new PointF(box.X + (box.Width - GlyphBox * scale) / 2 + unit.X * scale, box.Y + (box.Height - GlyphBox * scale) / 2 + unit.Y * scale);
    }

    private static Pen GlyphPen(Color color, Rectangle box) => new(color, GlyphStroke * Math.Min(box.Width, box.Height) / GlyphBox)
    {
        StartCap = LineCap.Round, EndCap = LineCap.Round, LineJoin = LineJoin.Round,
    };
}
