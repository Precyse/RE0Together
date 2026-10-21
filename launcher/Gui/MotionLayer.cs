using System.Buffers;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

namespace CoopLauncher.Gui;

/// <summary>Paints content as a layer: opacity, offset, scale about an origin and blur, the properties the broadcast
/// tool's recipes animate. A layer at rest paints straight onto the target; any other layer goes through an offscreen
/// bitmap that is blurred and then composited. The layer never paints outside its bounds.</summary>
internal static class MotionLayer
{
    private const float MinBlur = 0.5f;

    private static Bitmap? _scratch;

    /// <summary>Paints <paramref name="paint"/> (which draws into the rectangle it is given) as the layer inside
    /// <paramref name="bounds"/>. <paramref name="background"/> fills the offscreen first: text drawn by TextRenderer needs
    /// an opaque background to keep ClearType. The scale origin defaults to the centre of the bounds.</summary>
    public static void Paint(Graphics g, Rectangle bounds, Layer layer, Color background,
        Action<Graphics, Rectangle> paint, Point? origin = null)
    {
        if (layer == Layer.Rest)
        {
            paint(g, bounds);
            return;
        }
        if (layer.Opacity <= 0 || bounds.Width <= 0 || bounds.Height <= 0) return;

        var radius = layer.Blur >= MinBlur ? (int)Math.Round(Theme.ScaleF(layer.Blur)) : 0;
        var size = new Size(bounds.Width + 2 * radius, bounds.Height + 2 * radius);
        var scratch = Scratch(size);
        Render(scratch, size, radius, bounds.Size, background, paint);
        Composite(g, scratch, size, bounds, radius, layer, origin ?? new Point(bounds.X + bounds.Width / 2, bounds.Y + bounds.Height / 2));
    }

    private static Bitmap Scratch(Size size)
    {
        if (_scratch != null && _scratch.Width >= size.Width && _scratch.Height >= size.Height) return _scratch;
        var width = Math.Max(size.Width, _scratch?.Width ?? 0);
        var height = Math.Max(size.Height, _scratch?.Height ?? 0);
        _scratch?.Dispose();
        _scratch = new Bitmap(width, height, PixelFormat.Format32bppArgb);
        return _scratch;
    }

    private static void Render(Bitmap bitmap, Size size, int margin, Size content, Color background, Action<Graphics, Rectangle> paint)
    {
        using (var g = Graphics.FromImage(bitmap))
        {
            Draw.Fill(g, background, new Rectangle(Point.Empty, size));
            g.SetClip(new Rectangle(margin, margin, content.Width, content.Height));
            paint(g, new Rectangle(margin, margin, content.Width, content.Height));
        }
        if (margin > 0) Blur(bitmap, size, margin);
    }

    private static void Blur(Bitmap bitmap, Size size, int radius)
    {
        var data = bitmap.LockBits(new Rectangle(Point.Empty, size), ImageLockMode.ReadWrite, PixelFormat.Format32bppArgb);
        var length = data.Stride * size.Height;
        var pixels = ArrayPool<byte>.Shared.Rent(length);
        try
        {
            Marshal.Copy(data.Scan0, pixels, 0, length);
            BoxBlur.Apply(pixels, size.Width, size.Height, data.Stride, radius);
            Marshal.Copy(pixels, 0, data.Scan0, length);
        }
        finally
        {
            ArrayPool<byte>.Shared.Return(pixels);
            bitmap.UnlockBits(data);
        }
    }

    private static void Composite(Graphics g, Bitmap source, Size size, Rectangle bounds, int margin, Layer layer, Point origin)
    {
        var scale = layer.Scale;
        var left = origin.X + (bounds.X - margin - origin.X) * scale + Theme.ScaleF(layer.OffsetX);
        var top = origin.Y + (bounds.Y - margin - origin.Y) * scale + Theme.ScaleF(layer.OffsetY);
        var corners = new[]
        {
            new PointF(left, top), new PointF(left + size.Width * scale, top), new PointF(left, top + size.Height * scale),
        };
        using var attributes = new ImageAttributes();
        attributes.SetColorMatrix(new ColorMatrix { Matrix33 = Math.Clamp(layer.Opacity, 0f, 1f) });
        attributes.SetWrapMode(WrapMode.TileFlipXY);

        var state = g.Save();
        g.SetClip(bounds, CombineMode.Intersect);
        g.InterpolationMode = InterpolationMode.HighQualityBilinear;
        g.PixelOffsetMode = PixelOffsetMode.Half;
        g.DrawImage(source, corners, new RectangleF(0, 0, size.Width, size.Height), GraphicsUnit.Pixel, attributes);
        g.Restore(state);
    }
}

/// <summary>A separable box blur on BGRA pixels.</summary>
public static class BoxBlur
{
    /// <summary>Blurs the image in place: a horizontal and a vertical pass over every channel, each pixel the mean of the
    /// 2 * radius + 1 pixels around it with the edge pixel repeated beyond the image. Radius 0 changes nothing.</summary>
    public static void Apply(byte[] bgra, int width, int height, int stride, int radius)
    {
        if (radius <= 0) return;
        var temp = ArrayPool<byte>.Shared.Rent(bgra.Length);
        try
        {
            Pass(bgra, temp, width, height, BytesPerPixel, stride, radius);
            Pass(temp, bgra, height, width, stride, BytesPerPixel, radius);
        }
        finally
        {
            ArrayPool<byte>.Shared.Return(temp);
        }
    }

    private const int BytesPerPixel = 4;

    /// <summary>Blurs every line of <paramref name="source"/> into <paramref name="target"/>: <paramref name="lines"/>
    /// lines of <paramref name="length"/> samples, the samples <paramref name="step"/> bytes apart, the lines
    /// <paramref name="lineStep"/> bytes apart.</summary>
    private static void Pass(byte[] source, byte[] target, int length, int lines, int step, int lineStep, int radius)
    {
        var window = 2 * radius + 1;
        var half = window / 2;
        for (var line = 0; line < lines; line++)
        {
            for (var channel = 0; channel < BytesPerPixel; channel++)
            {
                var start = line * lineStep + channel;
                var sum = 0;
                for (var k = -radius; k <= radius; k++) sum += source[start + Math.Clamp(k, 0, length - 1) * step];
                for (var i = 0; i < length; i++)
                {
                    target[start + i * step] = (byte)((sum + half) / window);
                    sum += source[start + Math.Min(i + radius + 1, length - 1) * step] - source[start + Math.Max(i - radius, 0) * step];
                }
            }
        }
    }
}
