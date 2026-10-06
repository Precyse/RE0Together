namespace CoopLauncher.Gui;

/// <summary>The broadcast tool's operator look: flat, squared, grey, with colour spent on state only
/// (red = connected and live, amber = armed or waiting).</summary>
internal static class Theme
{
    private const float BaseDpi = 96f;
    private static readonly float DpiScale = ScreenScale();

    public static readonly Color Bg = ColorTranslator.FromHtml("#0c0d0e");
    public static readonly Color Bg2 = ColorTranslator.FromHtml("#141618");
    public static readonly Color Card = ColorTranslator.FromHtml("#1c1f22");
    public static readonly Color CardHover = ColorTranslator.FromHtml("#2e2e2e");
    public static readonly Color Selection = ColorTranslator.FromHtml("#2c3035");
    public static readonly Color Line = ColorTranslator.FromHtml("#2a2e33");
    public static readonly Color Text = ColorTranslator.FromHtml("#ececec");
    public static readonly Color Muted = ColorTranslator.FromHtml("#8b9197");
    public static readonly Color Dim = ColorTranslator.FromHtml("#5b6168");
    public static readonly Color Live = ColorTranslator.FromHtml("#e5202e");
    public static readonly Color Armed = ColorTranslator.FromHtml("#f2a900");

    public static readonly Font Body = new("Segoe UI", 9.75f);
    public static readonly Font Strong = new("Segoe UI Semibold", 9.75f);
    public static readonly Font Small = new("Segoe UI", 8.25f);
    public static readonly Font Label = new("Segoe UI", 8f, FontStyle.Bold);
    public static readonly Font Brand = new("Segoe UI", 10.5f, FontStyle.Bold);
    public static readonly Font Primary = new("Segoe UI", 9.75f, FontStyle.Bold);
    public static readonly Font Mono = new("Consolas", 8.25f);
    public static readonly Font Code = new("Consolas", 15f, FontStyle.Bold);

    public static readonly int LampSize = Scale(10);
    public static readonly int ControlHeight = Scale(36);
    public static readonly int SectionPadX = Scale(20);
    public static readonly int SectionPadY = Scale(16);
    public static readonly int Gap = Scale(8);

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
