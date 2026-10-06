namespace CoopLauncher.Gui;

/// <summary>The broadcast tool's operator look: flat, squared, grey, with colour spent on state only
/// (red = connected and live, amber = armed or waiting).</summary>
internal static class Theme
{
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

    public const int LampSize = 10;
    public const int ControlHeight = 36;
    public const int SectionPadX = 20;
    public const int SectionPadY = 16;
    public const int Gap = 8;
}
