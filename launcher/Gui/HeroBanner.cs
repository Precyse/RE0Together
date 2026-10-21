using System.Drawing.Drawing2D;

namespace CoopLauncher.Gui;

/// <summary>The selected game's Steam hero art, shaded from the left so its logo reads on top of it. The art runs on past
/// the banner into the caption band of the section below (<see cref="PaintBleed"/>), fading into the window background so
/// the two read as one surface.</summary>
internal sealed class HeroBanner : Control
{
    private static readonly int BannerHeight = Theme.Scale(120);
    private static readonly int LogoPadX = Theme.Scale(20);
    private static readonly int LogoPadBottom = Theme.Scale(18);
    private static readonly int LogoMaxWidth = Theme.Scale(260);
    private static readonly int LogoMaxHeight = Theme.Scale(90);
    private const int ShadeLeftAlpha = 235;
    private const int ShadeMidAlpha = 140;
    private const float ShadeMid = 0.45f;
    private const float ShadeEnd = 0.75f;
    private const float FadeStart = 0.5f;
    private const int FadeMidAlpha = 170;
    private const float FadeMid = 0.82f;

    private SteamArt? _art;
    private string _name = string.Empty;

    public HeroBanner()
    {
        Dock = DockStyle.Top;
        Height = BannerHeight;
        BackColor = Theme.Bg;
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer
                 | ControlStyles.ResizeRedraw, true);
    }

    /// <summary>Raised when the art changes, so the section carrying the bleed repaints with it.</summary>
    public event Action? ArtChanged;

    /// <summary>How far the art runs on below the banner.</summary>
    public int Bleed { get; set; }

    public void Show(GameProfile profile, SteamArt art)
    {
        _art = art;
        _name = profile.Name;
        Invalidate();
        ArtChanged?.Invoke();
    }

    /// <summary>Paints the part of the art that runs on below the banner, at the top of a control <paramref name="width"/>
    /// wide sitting directly under it.</summary>
    public void PaintBleed(Graphics g, int width)
    {
        if (_art?.Hero == null || Bleed <= 0) return;
        var state = g.Save();
        g.SetClip(new Rectangle(0, 0, width, Bleed));
        g.TranslateTransform(0, -Height);
        PaintArt(g, width);
        g.Restore(state);
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;
        g.Clear(BackColor);
        PaintArt(g, Width);

        var logoBox = new Rectangle(LogoPadX, Height - LogoPadBottom - LogoMaxHeight, LogoMaxWidth, LogoMaxHeight);
        if (_art?.Logo is { } logo) Draw.FitBottomLeft(g, logo, logoBox);
        else Draw.Text(g, _name.ToUpperInvariant(), Theme.Brand, Theme.Text, logoBox);
    }

    /// <summary>The art over the banner and its bleed as one rectangle, shaded from the left and faded at the bottom.</summary>
    private void PaintArt(Graphics g, int width)
    {
        if (_art?.Hero is not { } hero || width <= 0) return;
        var area = new Rectangle(0, 0, width, Height + Bleed);
        g.InterpolationMode = InterpolationMode.HighQualityBicubic;
        Draw.Cover(g, hero, area);
        PaintGradient(g, area, LinearGradientMode.Horizontal,
            [Color.FromArgb(ShadeLeftAlpha, Theme.Bg), Color.FromArgb(ShadeMidAlpha, Theme.Bg), Color.FromArgb(0, Theme.Bg), Color.FromArgb(0, Theme.Bg)],
            [0f, ShadeMid, ShadeEnd, 1f]);
        PaintGradient(g, area, LinearGradientMode.Vertical,
            [Color.FromArgb(0, Theme.Bg), Color.FromArgb(0, Theme.Bg), Color.FromArgb(FadeMidAlpha, Theme.Bg), Theme.Bg],
            [0f, FadeStart, FadeMid, 1f]);
    }

    private static void PaintGradient(Graphics g, Rectangle area, LinearGradientMode mode, Color[] colors, float[] positions)
    {
        using var brush = new LinearGradientBrush(area, Color.Black, Color.Black, mode);
        brush.InterpolationColors = new ColorBlend { Colors = colors, Positions = positions };
        g.FillRectangle(brush, area);
    }
}
