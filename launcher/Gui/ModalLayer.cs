namespace CoopLauncher.Gui;

/// <summary>The modal layer of the window: it covers the area under the top bar with a still picture of what was there and a black
/// scrim over it, and holds one popup panel centred on top. The panel opens and closes with the modal recipe (scale 0.96 and a
/// fade, 250 / 150 ms) while the scrim fades with it. A click on the scrim asks to dismiss; the owner closes the popup (Esc and
/// the close button do the same).</summary>
internal sealed class ModalLayer : Control
{
    private const string ScrimChannel = "scrim";

    private Bitmap? _below;
    private Control? _panel;
    private double _scrim;
    private bool _closing;

    public ModalLayer()
    {
        Visible = false;
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.Opaque, true);
    }

    /// <summary>The scrim was clicked.</summary>
    public event Action? Dismissed;

    /// <summary>A popup is showing (opening or open).</summary>
    public bool IsOpen => _panel != null && !_closing;

    /// <summary>The popup panel on show, or null.</summary>
    public Control? Panel => _panel;

    /// <summary>Opens the panel over <paramref name="below"/> (what is under the layer now; the layer owns it).</summary>
    public void Open(Control panel, Bitmap below)
    {
        _below?.Dispose();
        _below = below;
        _panel = panel;
        _closing = false;
        _scrim = 0;
        Controls.Add(panel);
        Visible = true;
        BringToFront();
        Center();
        Motion.Animate(this, ScrimChannel, 0, 1, Durations.ModalOpen, Easing.Out, FadeScrim);
        MotionOverlay.Play(panel, Durations.ModalOpen, Easing.Out, Recipes.ModalOpen, Theme.Bg2, Under(panel));
    }

    /// <summary>Closes the popup with the modal recipe, then runs <paramref name="done"/>.</summary>
    public void Close(Action? done = null)
    {
        if (_panel is not { } panel || _closing)
        {
            done?.Invoke();
            return;
        }
        _closing = true;
        Motion.Animate(this, ScrimChannel, Motion.ValueOf(this, ScrimChannel) ?? _scrim, 0, Durations.ModalClose, Easing.Out, FadeScrim);
        MotionOverlay.Play(panel, Durations.ModalClose, Easing.Out, Recipes.ModalClose, Theme.Bg2, Under(panel), done: () =>
        {
            Visible = false;
            Controls.Remove(panel);
            panel.Dispose();
            _panel = null;
            _closing = false;
            _below?.Dispose();
            _below = null;
            done?.Invoke();
        });
    }

    protected override void OnResize(EventArgs e)
    {
        Center();
        base.OnResize(e);
    }

    protected override void OnMouseDown(MouseEventArgs e)
    {
        if (IsOpen && _panel != null && !_panel.Bounds.Contains(e.Location)) Dismissed?.Invoke();
        base.OnMouseDown(e);
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;
        if (_below != null) g.DrawImageUnscaled(_below, 0, 0);
        else g.Clear(Theme.Bg);
        Draw.Fill(g, ScrimColor(_scrim), ClientRectangle);
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing) _below?.Dispose();
        base.Dispose(disposing);
    }

    private static Color ScrimColor(double amount) => Color.FromArgb((int)Math.Round(Theme.ScrimAlpha * amount), Color.Black);

    private void FadeScrim(double amount)
    {
        _scrim = amount;
        Invalidate();
    }

    private void Center()
    {
        if (_panel is { } panel) panel.Location = new Point((Width - panel.Width) / 2, (Height - panel.Height) / 2);
    }

    /// <summary>The picture behind the panel at the full scrim: what the panel's corners show while it scales.</summary>
    private Bitmap Under(Control panel)
    {
        var under = new Bitmap(panel.Width, panel.Height);
        using var g = Graphics.FromImage(under);
        if (_below != null) g.DrawImage(_below, -panel.Left, -panel.Top);
        Draw.Fill(g, ScrimColor(1), new Rectangle(Point.Empty, under.Size));
        return under;
    }
}
