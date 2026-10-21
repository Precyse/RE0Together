namespace CoopLauncher.Gui;

/// <summary>The area under the top bar: exactly one screen is visible at a time and a change of screen moves with the
/// recipe <see cref="Screens.Between"/> names (tabs and the forward step slide side by side, a screen opening over another is
/// revealed in and out again). The host is handed what to update between the old screen and the new, so a screen that
/// stays the same control (hosting becoming the lobby) is captured before and after. The overlays that play are
/// <see cref="MotionOverlay"/> for the reveal and <see cref="PageSwap"/> for the slide; with reduced motion the screen simply
/// swaps.</summary>
internal sealed class ScreenHost : Panel
{
    private readonly Dictionary<ScreenKind, Control> _screens = new();
    private ScreenKind? _current;
    private PageSwap? _swap;

    public ScreenHost()
    {
        BackColor = Theme.Bg;
        Dock = DockStyle.Fill;
    }

    /// <summary>The screen shown, or null before the first.</summary>
    public ScreenKind? Current => _current;

    /// <summary>Puts a screen in the host under the kinds it serves (hosting and the lobby are one control).</summary>
    public void Add(Control screen, params ScreenKind[] kinds)
    {
        screen.Dock = DockStyle.Fill;
        screen.Visible = false;
        Controls.Add(screen);
        foreach (var kind in kinds) _screens[kind] = screen;
    }

    /// <summary>Shows the screen of this kind. <paramref name="update"/> fills the screen for the new state; it runs after the
    /// outgoing screen was captured and with the incoming one visible (a hidden control cannot lay itself out), before the
    /// incoming one is captured.</summary>
    public void Show(ScreenKind kind, Action update)
    {
        var transition = _current is { } current ? Screens.Between(current, kind) : new Transition(TransitionKind.None);
        var from = _current is { } was ? _screens[was] : null;
        var to = _screens[kind];
        _swap?.Remove();
        var leaving = transition.Kind == TransitionKind.Page && from != null && !Motion.Reduced ? MotionOverlay.Capture(from) : null;
        if (from != null && from != to && transition.Kind != TransitionKind.PanelOut) from.Visible = false;
        to.Visible = true;
        update();
        _current = kind;
        if (transition.Kind == TransitionKind.None && from == to) return;

        to.PerformLayout();
        switch (transition.Kind)
        {
            case TransitionKind.PanelIn:
                MotionOverlay.Play(to, Durations.PanelOpen, Easing.Out, Recipes.PanelIn, Theme.Bg);
                break;
            case TransitionKind.PanelOut:
                PlayPanelOut(from!, to);
                break;
            case TransitionKind.Page when leaving != null:
                _swap = new PageSwap(this, leaving, MotionOverlay.Capture(to), transition.Direction);
                break;
        }
    }

    /// <summary>The leaving screen sinks away over the screen it reveals; it is hidden once its overlay has its snapshot.</summary>
    private static void PlayPanelOut(Control from, Control to)
    {
        var under = MotionOverlay.Capture(to);
        MotionOverlay.Play(from, Durations.PanelClose, Easing.Out, Recipes.PanelOut, Theme.Bg, under);
        from.Visible = false;
    }

    /// <summary>Two snapshots drawn over the host: the old page leaves to one side while the new one arrives from the other
    /// (page-side-by-side). It lets every click through to the live screen underneath and removes itself when done.</summary>
    private sealed class PageSwap : Control
    {
        private const string Channel = "page";

        private readonly Bitmap _old;
        private readonly Bitmap _new;
        private readonly int _direction;
        private double _progress;

        public PageSwap(Control host, Bitmap old, Bitmap arriving, int direction)
        {
            _old = old;
            _new = arriving;
            _direction = direction;
            Bounds = new Rectangle(Point.Empty, host.Size);
            SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.Opaque, true);
            TabStop = false;
            host.Controls.Add(this);
            BringToFront();
            _ = Handle;
            Motion.Animate(this, Channel, 0, 1, Durations.Page, Easing.Out, progress =>
            {
                _progress = progress;
                Invalidate();
            }, Remove);
        }

        public void Remove()
        {
            Parent?.Controls.Remove(this);
            Dispose();
        }

        protected override void WndProc(ref Message m)
        {
            if (m.Msg == NcHitTest.WindowMessage)
            {
                m.Result = NcHitTest.Transparent;
                return;
            }
            base.WndProc(ref m);
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            var g = e.Graphics;
            g.Clear(Theme.Bg);
            MotionLayer.Paint(g, ClientRectangle, Recipes.PageOut(_progress, _direction), Theme.Bg, (target, rect) => target.DrawImageUnscaled(_old, rect.Location));
            MotionLayer.Paint(g, ClientRectangle, Recipes.PageIn(_progress, _direction), Theme.Bg, (target, rect) => target.DrawImageUnscaled(_new, rect.Location));
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing)
            {
                _old.Dispose();
                _new.Dispose();
            }
            base.Dispose(disposing);
        }
    }
}
