using System.Drawing.Imaging;
using System.Runtime.InteropServices;

namespace CoopLauncher.Gui;

/// <summary>Pixel format conversion for Steam avatars and the pictures taken from a game.</summary>
public static class AvatarPixels
{
    private const int BytesPerPixel = 4;

    /// <summary>Steam's RGBA bytes as the memory order of <see cref="PixelFormat.Format32bppArgb"/> (blue, green, red,
    /// alpha); alpha is kept.</summary>
    public static byte[] RgbaToBgra(ReadOnlySpan<byte> rgba, int width, int height)
    {
        if (width < 0 || height < 0 || rgba.Length != width * height * BytesPerPixel)
            throw new ArgumentException($"{rgba.Length} bytes are not a {width}x{height} RGBA image", nameof(rgba));
        var bgra = new byte[rgba.Length];
        for (var i = 0; i < rgba.Length; i += BytesPerPixel)
        {
            bgra[i] = rgba[i + 2];
            bgra[i + 1] = rgba[i + 1];
            bgra[i + 2] = rgba[i];
            bgra[i + 3] = rgba[i + 3];
        }
        return bgra;
    }

    /// <summary>A bitmap holding BGRA bytes (<paramref name="width"/> x <paramref name="height"/> pixels, rows without padding).</summary>
    public static Bitmap ToBitmap(byte[] bgra, int width, int height)
    {
        var bitmap = new Bitmap(width, height, PixelFormat.Format32bppArgb);
        var data = bitmap.LockBits(new Rectangle(0, 0, width, height), ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
        try
        {
            var row = width * BytesPerPixel;
            for (var y = 0; y < height; y++) Marshal.Copy(bgra, y * row, data.Scan0 + y * data.Stride, row);
        }
        finally
        {
            bitmap.UnlockBits(data);
        }
        return bitmap;
    }
}
