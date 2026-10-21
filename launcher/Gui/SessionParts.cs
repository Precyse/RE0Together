using System.Drawing.Drawing2D;
using System.Drawing.Imaging;

namespace CoopLauncher.Gui;

/// <summary>Painting the session screens share: the game's logo, a host's avatar tile and the art laid under a screen.</summary>
internal static class SessionArt
{
    /// <summary>The logo scaled to fit the box without cropping, at its left edge and centred vertically.</summary>
    public static void PaintLogo(Graphics g, Image? logo, Rectangle box)
    {
        if (logo == null) return;
        var scale = Math.Min((float)box.Width / logo.Width, (float)box.Height / logo.Height);
        var width = (int)(logo.Width * scale);
        var height = (int)(logo.Height * scale);
        var state = g.Save();
        g.InterpolationMode = System.Drawing.Drawing2D.InterpolationMode.HighQualityBicubic;
        g.DrawImage(logo, new Rectangle(box.X, box.Y + (box.Height - height) / 2, width, height));
        g.Restore(state);
    }

    /// <summary>A player's avatar in the box, or its initial on the raised gray (the host's placeholder).</summary>
    public static void PaintAvatar(Graphics g, AvatarStore avatars, ulong id, string name, Rectangle box, Font initialFont)
    {
        if (avatars.Get(id) != null)
        {
            avatars.Paint(g, box, id, name);
            return;
        }
        Draw.Fill(g, Theme.Raised, box);
        if (name.Trim() is { Length: > 0 } trimmed) Draw.Centered(g, char.ToUpperInvariant(trimmed[0]).ToString(), initialFont, Theme.Muted, box);
    }
}

/// <summary>The hero art laid under a screen at a low opacity, rendered once per size and art so that every repaint of a
/// transparent child over it is a plain copy.</summary>
internal sealed class BackdropLayer : IDisposable
{
    private Bitmap? _cache;
    private Image? _source;
    private float _opacity;

    public void Paint(Graphics g, Size size, Image? art, float opacity)
    {
        if (size.Width <= 0 || size.Height <= 0) return;
        if (_cache == null || _cache.Size != size || !ReferenceEquals(_source, art) || _opacity != opacity) Render(size, art, opacity);
        g.DrawImageUnscaled(_cache!, 0, 0);
    }

    public void Dispose() => _cache?.Dispose();

    private void Render(Size size, Image? art, float opacity)
    {
        _cache?.Dispose();
        _cache = new Bitmap(size.Width, size.Height, PixelFormat.Format24bppRgb);
        _source = art;
        _opacity = opacity;
        using var g = Graphics.FromImage(_cache);
        g.Clear(Theme.Bg);
        if (art == null) return;
        var rect = new Rectangle(Point.Empty, size);
        g.InterpolationMode = InterpolationMode.HighQualityBicubic;
        g.PixelOffsetMode = PixelOffsetMode.Half;
        Draw.Cover(g, art, rect);
        using var veil = new SolidBrush(Color.FromArgb((int)Math.Round(255 * (1 - opacity)), Theme.Bg));
        g.FillRectangle(veil, rect);
    }
}

/// <summary>A square icon button over what is behind it: a card fill fades in on hover, Space and Enter click it, a focus
/// border shows on keyboard focus. The subclass paints the glyph.</summary>
internal abstract class GlyphButton : Control
{
    private const string HoverChannel = "hover";
    private const int HoverFillAlpha = 220;

    private double _hover;

    protected GlyphButton(int size, string accessibleName)
    {
        Size = new Size(size, size);
        Cursor = Cursors.Hand;
        TabStop = true;
        AccessibleName = accessibleName;
        AccessibleRole = AccessibleRole.PushButton;
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer
                 | ControlStyles.ResizeRedraw | ControlStyles.Selectable | ControlStyles.StandardClick | ControlStyles.SupportsTransparentBackColor, true);
        BackColor = Color.Transparent;
    }

    protected abstract void PaintGlyph(Graphics g, Rectangle bounds);

    protected override void OnMouseEnter(EventArgs e)
    {
        Fade(1);
        base.OnMouseEnter(e);
    }

    protected override void OnMouseLeave(EventArgs e)
    {
        Fade(0);
        base.OnMouseLeave(e);
    }

    protected override bool IsInputKey(Keys keyData) => keyData is Keys.Space or Keys.Enter || base.IsInputKey(keyData);

    protected override void OnKeyDown(KeyEventArgs e)
    {
        if (e.KeyCode is Keys.Space or Keys.Enter)
        {
            e.Handled = true;
            OnClick(EventArgs.Empty);
        }
        base.OnKeyDown(e);
    }

    protected override void OnGotFocus(EventArgs e)
    {
        Invalidate();
        base.OnGotFocus(e);
    }

    protected override void OnLostFocus(EventArgs e)
    {
        Invalidate();
        base.OnLostFocus(e);
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        if (_hover > 0) Draw.Fill(e.Graphics, Color.FromArgb((int)(HoverFillAlpha * _hover), Theme.Card), ClientRectangle);
        PaintGlyph(e.Graphics, ClientRectangle);
        if (Focused && ShowFocusCues) Draw.Border(e.Graphics, Rectangle.Inflate(ClientRectangle, -1, -1), Theme.Text);
    }

    private void Fade(double target) =>
        Motion.Animate(this, HoverChannel, _hover, target, Durations.Hover, Easing.Css, value =>
        {
            _hover = value;
            Invalidate();
        });
}

/// <summary>The mini window's expand button: two corner brackets in a 36 px square.</summary>
internal sealed class ExpandButton : GlyphButton
{
    private static readonly int Glyph = Theme.Scale(12);

    public ExpandButton() : base(Theme.Scale(36), "Expand")
    {
    }

    protected override void PaintGlyph(Graphics g, Rectangle bounds) =>
        Draw.Expand(g, Theme.Text, new Rectangle(bounds.X + (bounds.Width - Glyph) / 2, bounds.Y + (bounds.Height - Glyph) / 2, Glyph, Glyph));
}

/// <summary>The copy button: a clipboard glyph that swaps to a green check when clicked (scale 0.25 to 1 with a blur, 250 ms)
/// and back after the hold.</summary>
internal sealed class CopyCodeButton : GlyphButton
{
    private const string ClipboardGlyph = "";
    private const string SwapChannel = "swap";
    private const float HiddenScale = 0.25f;
    private const int CheckSize = 18;

    private readonly System.Windows.Forms.Timer _hold = new() { Interval = Durations.CopyHold };
    private double _swap;

    public CopyCodeButton() : base(Theme.Scale(40), "Copy") =>
        _hold.Tick += (_, _) =>
        {
            _hold.Stop();
            Swap(0);
        };

    /// <summary>The check is showing (the copy was just made).</summary>
    public bool Copied => _swap > 0;

    /// <summary>Shows the check now and the clipboard again after <see cref="Durations.CopyHold"/>.</summary>
    public void Flash()
    {
        Swap(1);
        _hold.Stop();
        _hold.Start();
    }

    protected override void OnClick(EventArgs e)
    {
        Flash();
        base.OnClick(e);
    }

    protected override void PaintGlyph(Graphics g, Rectangle bounds)
    {
        MotionLayer.Paint(g, bounds, new Layer(1 - (float)_swap, 0, 0, Scaled(1 - _swap), Distances.Blur * (float)_swap), Color.Transparent,
            (target, area) => Draw.Centered(target, ClipboardGlyph, Theme.Icon, Theme.Text, area));
        MotionLayer.Paint(g, bounds, new Layer((float)_swap, 0, 0, Scaled(_swap), Distances.Blur * (float)(1 - _swap)), Color.Transparent,
            (target, area) => Draw.Check(target, Theme.Connected, CheckBox(area), 1));
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing) _hold.Dispose();
        base.Dispose(disposing);
    }

    private static float Scaled(double visible) => (float)Interpolate.Lerp(HiddenScale, 1, visible);

    private static Rectangle CheckBox(Rectangle area)
    {
        var side = Theme.Scale(CheckSize);
        return new Rectangle(area.X + (area.Width - side) / 2, area.Y + (area.Height - side) / 2, side, side);
    }

    private void Swap(double target) =>
        Motion.Animate(this, SwapChannel, _swap, target, Durations.CopySwap, Easing.InOut, value =>
        {
            _swap = value;
            Invalidate();
        });
}
