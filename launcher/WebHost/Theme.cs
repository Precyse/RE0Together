namespace CoopLauncher.Gui;

/// <summary>The window host's colours and metrics: the page's own backgrounds shown behind it while it loads, and sizes
/// converted to the screen's scale.</summary>
internal static class Theme
{
    private const float BaseDpi = 96f;
    private static readonly float DpiScale = ScreenScale();

    public static readonly Color Bg = ColorTranslator.FromHtml("#0c0d0e");
    public static readonly Color Bg2 = ColorTranslator.FromHtml("#141618");
    public static readonly Color Line = ColorTranslator.FromHtml("#2a2e33");

    /// <summary>The raised grey of an outfit silhouette.</summary>
    public static readonly Color Raised = ColorTranslator.FromHtml("#3a3f45");

    /// <summary>The band along the top edge of the chromeless window that resizes it.</summary>
    public static readonly int ResizeBand = Scale(5);

    /// <summary>A size in 96 dpi units as pixels at the screen's scale. The process is system DPI aware, so the scale is
    /// fixed for its lifetime; every metric is converted once.</summary>
    public static int Scale(int pixelsAt96Dpi) => (int)Math.Round(pixelsAt96Dpi * DpiScale);

    /// <summary>The inverse of <see cref="Scale"/>.</summary>
    public static int Unscale(int pixels) => (int)Math.Round(pixels / DpiScale);

    private static float ScreenScale()
    {
        using var screen = Graphics.FromHwnd(IntPtr.Zero);
        return screen.DpiX / BaseDpi;
    }
}
