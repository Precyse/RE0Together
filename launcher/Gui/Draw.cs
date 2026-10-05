namespace CoopLauncher.Gui;

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

    public static int Width(string text, Font font) => TextRenderer.MeasureText(text, font).Width;

    private const TextFormatFlags Wrap = TextFormatFlags.Left | TextFormatFlags.Top | TextFormatFlags.WordBreak
        | TextFormatFlags.NoPrefix | TextFormatFlags.EndEllipsis;

    /// <summary>Text wrapped to the width, top-aligned in the bounds.</summary>
    public static void Wrapped(Graphics g, string text, Font font, Color color, Rectangle bounds) =>
        TextRenderer.DrawText(g, text, font, bounds, color, Wrap);

    /// <summary>The height of the text wrapped to the width, capped at the given number of lines.</summary>
    public static int WrappedHeight(string text, Font font, int width, int maxLines) =>
        Math.Min(TextRenderer.MeasureText(text, font, new Size(width, int.MaxValue), Wrap).Height, font.Height * maxLines);

    /// <summary>A section or status label: small bold caps.</summary>
    public static void Caption(Graphics g, string text, Color color, Point at) =>
        TextRenderer.DrawText(g, text.ToUpperInvariant(), Theme.Label, at, color, TextFormatFlags.NoPrefix);

    /// <summary>A state lamp: a solid square, never a dot.</summary>
    public static void Lamp(Graphics g, Color color, int x, int centerY)
    {
        using var brush = new SolidBrush(color);
        g.FillRectangle(brush, x, centerY - Theme.LampSize / 2, Theme.LampSize, Theme.LampSize);
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

    public static void Border(Graphics g, Rectangle bounds, Color color)
    {
        using var pen = new Pen(color);
        g.DrawRectangle(pen, bounds.X, bounds.Y, bounds.Width - 1, bounds.Height - 1);
    }

    /// <summary>Fills the rectangle with the image, cropping whatever overflows (CSS object-fit: cover).</summary>
    public static void Cover(Graphics g, Image image, Rectangle dest)
    {
        var scale = Math.Max((float)dest.Width / image.Width, (float)dest.Height / image.Height);
        var cropWidth = dest.Width / scale;
        var cropHeight = dest.Height / scale;
        var source = new RectangleF((image.Width - cropWidth) / 2, (image.Height - cropHeight) / 2, cropWidth, cropHeight);
        g.DrawImage(image, dest, source, GraphicsUnit.Pixel);
    }

    /// <summary>The image scaled to fit inside the box without cropping, anchored to the box's bottom-left corner.</summary>
    public static void FitBottomLeft(Graphics g, Image image, Rectangle box)
    {
        var scale = Math.Min((float)box.Width / image.Width, (float)box.Height / image.Height);
        var width = (int)(image.Width * scale);
        var height = (int)(image.Height * scale);
        g.DrawImage(image, new Rectangle(box.X, box.Bottom - height, width, height));
    }
}
