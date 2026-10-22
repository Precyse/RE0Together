using System.Drawing.Imaging;
using CoopLauncher.Gui;

namespace LauncherTests;

/// <summary>Avatar pixel conversion.</summary>
public static class AvatarTests
{
    public static void Run()
    {
        Pixels();
        Bitmaps();
    }

    private static void Pixels()
    {
        var bgra = AvatarPixels.RgbaToBgra(new byte[] { 1, 2, 3, 4, 5, 6, 7, 8 }, 2, 1);
        Check.That(bgra.SequenceEqual(new byte[] { 3, 2, 1, 4, 7, 6, 5, 8 }), "RGBA becomes BGRA, alpha kept");
        var threw = false;
        try
        {
            AvatarPixels.RgbaToBgra(new byte[7], 2, 1);
        }
        catch (ArgumentException)
        {
            threw = true;
        }
        Check.That(threw, "a wrong length throws");
    }

    private static void Bitmaps()
    {
        using var bitmap = AvatarPixels.ToBitmap(AvatarPixels.RgbaToBgra(new byte[] { 10, 20, 30, 255, 40, 50, 60, 128 }, 2, 1), 2, 1);
        Check.That(bitmap.PixelFormat == PixelFormat.Format32bppArgb, "the bitmap is 32bpp ARGB");
        Check.That(bitmap.GetPixel(0, 0) == Color.FromArgb(255, 10, 20, 30) && bitmap.GetPixel(1, 0) == Color.FromArgb(128, 40, 50, 60), "the pixels keep their colours and alpha");
    }
}
