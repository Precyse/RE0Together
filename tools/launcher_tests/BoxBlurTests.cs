using CoopLauncher.Gui;

namespace LauncherTests;

/// <summary>BoxBlur: a separable box blur on BGRA bytes with edge clamping.</summary>
public static class BoxBlurTests
{
    private const int Size = 7;
    private const int BytesPerPixel = 4;
    private const int Stride = Size * BytesPerPixel + 8;
    private const int Centre = 3;
    private const byte White = 255;

    public static void Run()
    {
        RadiusZeroChangesNothing();
        SinglePixelSpreadsSymmetrically();
        ConstantImageIsUnchanged();
        EdgesAreClamped();
    }

    private static byte[] Blank() => new byte[Stride * Size];

    private static int Index(int x, int y, int channel = 0) => y * Stride + x * BytesPerPixel + channel;

    private static byte[] WithWhitePixel(int x, int y)
    {
        var image = Blank();
        for (var channel = 0; channel < BytesPerPixel; channel++) image[Index(x, y, channel)] = White;
        return image;
    }

    private static void RadiusZeroChangesNothing()
    {
        var image = WithWhitePixel(Centre, Centre);
        var copy = (byte[])image.Clone();
        BoxBlur.Apply(image, Size, Size, Stride, 0);
        Check.That(image.SequenceEqual(copy), "radius 0 leaves the image alone");
    }

    private static void SinglePixelSpreadsSymmetrically()
    {
        var image = WithWhitePixel(Centre, Centre);
        BoxBlur.Apply(image, Size, Size, Stride, 1);
        var expected = (byte)(((White + 1) / 3 + 1) / 3);
        var ok = true;
        for (var y = 0; y < Size; y++)
            for (var x = 0; x < Size; x++)
            {
                var inside = Math.Abs(x - Centre) <= 1 && Math.Abs(y - Centre) <= 1;
                for (var channel = 0; channel < BytesPerPixel; channel++)
                    ok &= image[Index(x, y, channel)] == (inside ? expected : 0);
            }
        Check.That(ok, $"a white pixel becomes a 3x3 block of {expected} at radius 1");
        Check.That(image[Index(Centre - 1, Centre)] == image[Index(Centre + 1, Centre)]
                   && image[Index(Centre, Centre - 1)] == image[Index(Centre, Centre + 1)], "the spread is symmetric");
    }

    private static void ConstantImageIsUnchanged()
    {
        var image = Blank();
        for (var y = 0; y < Size; y++)
            for (var x = 0; x < Size; x++)
                for (var channel = 0; channel < BytesPerPixel; channel++) image[Index(x, y, channel)] = (byte)(40 + channel * 30);
        var copy = (byte[])image.Clone();
        BoxBlur.Apply(image, Size, Size, Stride, 2);
        var same = true;
        for (var y = 0; y < Size; y++)
            for (var x = 0; x < Size; x++)
                for (var channel = 0; channel < BytesPerPixel; channel++) same &= image[Index(x, y, channel)] == copy[Index(x, y, channel)];
        Check.That(same, "a constant image is unchanged, edges included");
    }

    private static void EdgesAreClamped()
    {
        var image = WithWhitePixel(0, 0);
        BoxBlur.Apply(image, Size, Size, Stride, 1);
        const int rowPass = (White * 2 + 1) / 3;
        const int expected = (rowPass * 2 + 1) / 3;
        Check.That(image[Index(0, 0)] == expected, $"the edge pixel is repeated beyond the image: the corner keeps {expected} of 255");
    }
}
