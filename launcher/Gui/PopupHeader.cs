using System.Drawing.Drawing2D;

namespace CoopLauncher.Gui;

/// <summary>The top of a popup: the game's art covering it, fading into the panel's grey, the game's logo at the bottom left and
/// the close button at the top right.</summary>
internal sealed class PopupHeader : Control
{
    private const int GradientTopAlpha = 26;
    private const float GradientStart = 0.3f;

    private static readonly int HeaderHeight = Theme.Scale(140);
    private static readonly Rectangle LogoBox = new(Theme.Scale(20), Theme.Scale(140 - 14 - 60), Theme.Scale(150), Theme.Scale(60));
    private static readonly int CloseInset = Theme.Scale(6);

    private readonly CloseButton _close = new();
    private SteamArt? _art;

    public PopupHeader()
    {
        Height = HeaderHeight;
        Dock = DockStyle.Top;
        BackColor = Theme.Bg2;
        Controls.Add(_close);
        _close.Click += (_, _) => CloseClicked?.Invoke();
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer
                 | ControlStyles.ResizeRedraw, true);
    }

    /// <summary>The close button was clicked.</summary>
    public event Action? CloseClicked;

    public void Show(SteamArt art)
    {
        _art = art;
        Invalidate();
    }

    protected override void OnResize(EventArgs e)
    {
        _close.Location = new Point(Width - CloseInset - _close.Width, CloseInset);
        base.OnResize(e);
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;
        g.Clear(BackColor);
        if (_art?.Hero is { } hero)
        {
            g.InterpolationMode = InterpolationMode.HighQualityBicubic;
            Draw.Cover(g, hero, ClientRectangle);
            using var brush = new LinearGradientBrush(ClientRectangle, Color.Black, Color.Black, LinearGradientMode.Vertical);
            brush.InterpolationColors = new ColorBlend
            {
                Colors = [Color.FromArgb(GradientTopAlpha, Theme.Bg2), Color.FromArgb(GradientTopAlpha, Theme.Bg2), Theme.Bg2],
                Positions = [0f, GradientStart, 1f],
            };
            g.FillRectangle(brush, ClientRectangle);
        }
        SessionArt.PaintLogo(g, _art?.Logo, LogoBox);
    }
}

/// <summary>The popup's close button: a 40 px square on the background grey at 70 %, with an X.</summary>
internal sealed class CloseButton : GlyphButton
{
    private const int FillAlpha = 178;

    private static readonly int GlyphBoxSize = Theme.Scale(22);

    public CloseButton() : base(Theme.Scale(40), "Close")
    {
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        Draw.Fill(e.Graphics, Color.FromArgb(FillAlpha, Theme.Bg), ClientRectangle);
        base.OnPaint(e);
    }

    protected override void PaintGlyph(Graphics g, Rectangle bounds) =>
        Draw.Cross(g, Theme.Text, new Rectangle(bounds.X + (bounds.Width - GlyphBoxSize) / 2, bounds.Y + (bounds.Height - GlyphBoxSize) / 2, GlyphBoxSize, GlyphBoxSize));
}
