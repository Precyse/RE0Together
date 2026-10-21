using System.Drawing.Imaging;

namespace CoopLauncher.Gui;

/// <summary>Animates a composite control (one with child windows) without touching it: a snapshot of the control at its
/// final layout is drawn over it as a layer while the real controls stay live underneath, and the overlay lets every
/// click through to them at once.</summary>
internal static class MotionOverlay
{
    private const string Channel = "overlay";

    private static readonly Dictionary<Control, Overlay> _playing = new();

    /// <summary>Plays the recipe over the target for the duration. The overlay paints <paramref name="under"/> (or the
    /// background) and then the snapshot as the recipe's layer; it takes ownership of <paramref name="under"/>. Starting
    /// a play on a target cancels its running one; <paramref name="done"/> runs when the overlay ends, not when it is
    /// cancelled. With reduced motion, or a target not on screen, nothing is drawn and done runs at once.</summary>
    public static void Play(Control target, int durationMs, CubicBezier curve, Func<double, Layer> recipe, Color background,
        Bitmap? under = null, Action? done = null)
    {
        Cancel(target);
        if (Motion.Reduced || target.Parent is not { IsHandleCreated: true } parent || !target.IsHandleCreated)
        {
            under?.Dispose();
            done?.Invoke();
            return;
        }

        var overlay = new Overlay(Capture(target), under, background, recipe) { Bounds = target.Bounds };
        parent.Controls.Add(overlay);
        overlay.BringToFront();
        _ = overlay.Handle;
        _playing[target] = overlay;
        overlay.Disposed += (_, _) => _playing.Remove(target);
        Motion.Animate(overlay, Channel, 0, 1, durationMs, curve,
            progress => overlay.SetProgress(progress),
            () =>
            {
                overlay.Remove();
                done?.Invoke();
            });
    }

    /// <summary>Removes the target's running overlay at once.</summary>
    public static void Cancel(Control target)
    {
        if (_playing.TryGetValue(target, out var overlay)) overlay.Remove();
    }

    /// <summary>The control and its children as they are drawn now.</summary>
    public static Bitmap Capture(Control control)
    {
        var bitmap = new Bitmap(Math.Max(1, control.Width), Math.Max(1, control.Height), PixelFormat.Format32bppArgb);
        control.DrawToBitmap(bitmap, new Rectangle(Point.Empty, control.Size));
        return bitmap;
    }

    private sealed class Overlay : Control
    {
        private readonly Bitmap _snapshot;
        private readonly Bitmap? _under;
        private readonly Color _background;
        private readonly Func<double, Layer> _recipe;
        private double _progress;

        public Overlay(Bitmap snapshot, Bitmap? under, Color background, Func<double, Layer> recipe)
        {
            _snapshot = snapshot;
            _under = under;
            _background = background;
            _recipe = recipe;
            SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer
                     | ControlStyles.Opaque, true);
            TabStop = false;
        }

        public void SetProgress(double progress)
        {
            _progress = progress;
            Invalidate();
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
            if (_under != null) g.DrawImageUnscaled(_under, 0, 0);
            else g.Clear(_background);
            MotionLayer.Paint(g, ClientRectangle, _recipe(_progress), _background,
                (target, rect) => target.DrawImageUnscaled(_snapshot, rect.Location));
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing)
            {
                _snapshot.Dispose();
                _under?.Dispose();
            }
            base.Dispose(disposing);
        }
    }
}
