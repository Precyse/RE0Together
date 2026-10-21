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
    public static readonly Color Connected = ColorTranslator.FromHtml("#2ea043");
    public static readonly Color Problem = ColorTranslator.FromHtml("#e5202e");
    public static readonly Color Armed = ColorTranslator.FromHtml("#f2a900");

    /// <summary>The raised grey of an avatar placeholder, an open seat's dashes and the spinner's resting dots.</summary>
    public static readonly Color Raised = ColorTranslator.FromHtml("#3a3f45");

    /// <summary>The ink of a control that cannot be used.</summary>
    public static readonly Color Disabled = ColorTranslator.FromHtml("#4a4f55");

    /// <summary>Alpha (of 255) of the black scrim under a popup: 62 %.</summary>
    public const int ScrimAlpha = 158;

    /// <summary>Alpha of a card's fill over art: 92 %.</summary>
    public const int CardOverArtAlpha = 235;

    private const int TintGreenAlpha = 51;
    private const int TintAmberAlpha = 46;
    private const int TintRedAlpha = 56;

    /// <summary>The transparent tints a card's state lays over its gray: green 20 %, amber 18 %, red 22 %.</summary>
    public static readonly Color TintGreen = Color.FromArgb(TintGreenAlpha, Connected);
    public static readonly Color TintAmber = Color.FromArgb(TintAmberAlpha, Armed);
    public static readonly Color TintRed = Color.FromArgb(TintRedAlpha, Problem);

    public static readonly Font Body = new("Segoe UI", 9.75f);
    public static readonly Font Strong = new("Segoe UI Semibold", 9.75f);
    public static readonly Font Small = new("Segoe UI", 8.25f);
    public static readonly Font Label = new("Segoe UI", 8f, FontStyle.Bold);
    public static readonly Font Brand = new("Segoe UI", 10.5f, FontStyle.Bold);
    public static readonly Font Primary = new("Segoe UI", 9.75f, FontStyle.Bold);
    public static readonly Font Mono = new("Consolas", 8.25f);
    public static readonly Font Code = new("Consolas", 15f, FontStyle.Bold);
    public static readonly Font Icon = new("Segoe MDL2 Assets", 13f);

    /// <summary>Screen titles: 18 px semibold.</summary>
    public static readonly Font Title = new("Segoe UI Semibold", 13.5f);

    /// <summary>A card's name on the wide layout: 16 px semibold.</summary>
    public static readonly Font Name = new("Segoe UI Semibold", 12f);

    /// <summary>A card's name on the tall layout of two seats and the select's outfit label: 15 px semibold.</summary>
    public static readonly Font NameTall = new("Segoe UI Semibold", 11.25f);

    /// <summary>A card's name on the compact layouts: 12 px semibold.</summary>
    public static readonly Font NameSmall = new("Segoe UI Semibold", 9f);

    /// <summary>A step's text on a large list: 14 px.</summary>
    public static readonly Font Step = new("Segoe UI", 10.5f);
    public static readonly Font StepStrong = new("Segoe UI Semibold", 10.5f);

    /// <summary>A step's text on a small list and a card's state words: 12 px.</summary>
    public static readonly Font StepSmall = new("Segoe UI", 9f);
    public static readonly Font StepSmallStrong = new("Segoe UI Semibold", 9f);

    /// <summary>The select's character names: 22 px bold.</summary>
    public static readonly Font Hero = new("Segoe UI", 16.5f, FontStyle.Bold);

    public static readonly Font CodeLarge = new("Consolas", 18f, FontStyle.Bold);
    public static readonly Font CodeMedium = new("Consolas", 16.5f, FontStyle.Bold);

    /// <summary>Timers and counters: 14 px mono.</summary>
    public static readonly Font MonoMedium = new("Consolas", 10.5f);

    /// <summary>Trailing numbers of a large step list: 13 px mono.</summary>
    public static readonly Font MonoBody = new("Consolas", 9.75f);

    /// <summary>Trailing numbers of a small step list and the version pairs: 12 px mono.</summary>
    public static readonly Font MonoStep = new("Consolas", 9f);

    /// <summary>The initial of an avatar placeholder: 30 px bold, 40 px bold, and 11 px bold on the compact layouts.</summary>
    public static readonly Font Initial = new("Segoe UI", 22.5f, FontStyle.Bold);
    public static readonly Font InitialLarge = new("Segoe UI", 30f, FontStyle.Bold);
    public static readonly Font InitialSmall = new("Segoe UI", 8.25f, FontStyle.Bold);

    public static readonly int LampSize = Scale(10);
    public static readonly int ControlHeight = Scale(36);
    public static readonly int SectionPadX = Scale(20);
    public static readonly int SectionPadY = Scale(16);
    public static readonly int Gap = Scale(8);
    public static readonly int ArrowSize = Scale(12);

    /// <summary>The band along the top edge of the chromeless window that resizes it.</summary>
    public static readonly int ResizeBand = Scale(5);
    public static readonly int PrimaryWidth = Scale(120);
    public static readonly int ToolWidth = Scale(104);

    /// <summary>The widest a pane section's content grows; wider windows leave the rest of the row empty.</summary>
    public static readonly int ContentMaxWidth = Scale(760);

    /// <summary>A size in 96 dpi units as pixels at the screen's scale. The process is system DPI aware, so the scale is
    /// fixed for its lifetime; every metric is converted once.</summary>
    public static int Scale(int pixelsAt96Dpi) => (int)Math.Round(pixelsAt96Dpi * DpiScale);

    /// <summary>A fractional size in 96 dpi units as pixels at the screen's scale (animated offsets and blur radii).</summary>
    public static float ScaleF(float pixelsAt96Dpi) => pixelsAt96Dpi * DpiScale;

    /// <summary>The inverse of <see cref="Scale"/>.</summary>
    public static int Unscale(int pixels) => (int)Math.Round(pixels / DpiScale);

    private static float ScreenScale()
    {
        using var screen = Graphics.FromHwnd(IntPtr.Zero);
        return screen.DpiX / BaseDpi;
    }
}
