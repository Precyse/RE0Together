using System.Drawing.Drawing2D;

namespace CoopLauncher.Gui;

/// <summary>The selected game's Steam hero art, shaded from the left so its logo reads on top of it.</summary>
internal sealed class HeroBanner : Control
{
    private static readonly int BannerHeight = Theme.Scale(170);
    private static readonly int LogoPadX = Theme.Scale(20);
    private static readonly int LogoPadBottom = Theme.Scale(18);
    private static readonly int LogoMaxWidth = Theme.Scale(260);
    private static readonly int LogoMaxHeight = Theme.Scale(90);
    private const int ShadeLeftAlpha = 235;
    private const int ShadeMidAlpha = 140;
    private const float ShadeMid = 0.45f;
    private const float ShadeEnd = 0.75f;

    private SteamArt? _art;
    private string _name = string.Empty;

    public HeroBanner()
    {
        Dock = DockStyle.Top;
        Height = BannerHeight;
        BackColor = Theme.Card;
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer
                 | ControlStyles.ResizeRedraw, true);
    }

    public void Show(GameProfile profile, SteamArt art)
    {
        _art = art;
        _name = profile.Name;
        Invalidate();
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;
        g.Clear(BackColor);
        g.InterpolationMode = InterpolationMode.HighQualityBicubic;
        if (_art?.Hero is { } hero)
        {
            Draw.Cover(g, hero, ClientRectangle);
            PaintShade(g);
        }

        var logoBox = new Rectangle(LogoPadX, Height - LogoPadBottom - LogoMaxHeight, LogoMaxWidth, LogoMaxHeight);
        if (_art?.Logo is { } logo) Draw.FitBottomLeft(g, logo, logoBox);
        else Draw.Text(g, _name.ToUpperInvariant(), Theme.Brand, Theme.Text, logoBox);

        Draw.HorizontalLine(g, 0, Width, Height - 1);
    }

    private void PaintShade(Graphics g)
    {
        if (Width <= 0) return;
        using var shade = new LinearGradientBrush(ClientRectangle, Color.Black, Color.Black, LinearGradientMode.Horizontal);
        shade.InterpolationColors = new ColorBlend
        {
            Colors = new[]
            {
                Color.FromArgb(ShadeLeftAlpha, Theme.Bg), Color.FromArgb(ShadeMidAlpha, Theme.Bg),
                Color.FromArgb(0, Theme.Bg), Color.FromArgb(0, Theme.Bg),
            },
            Positions = new[] { 0f, ShadeMid, ShadeEnd, 1f },
        };
        g.FillRectangle(shade, ClientRectangle);
    }
}
