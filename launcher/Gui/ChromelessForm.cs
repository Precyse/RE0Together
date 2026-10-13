using System.Runtime.InteropServices;

namespace CoopLauncher.Gui;

/// <summary>A resizable window whose client area covers the system title bar. The frame stays a normal sizable frame
/// underneath, so Windows keeps the shadow, the rounded corners, Snap, the taskbar entry, Alt+F4, the minimize and
/// maximize animations and the maximized bounds. The left, right and bottom resize borders stay native; the top band
/// and the caption (the derived window's top strip) are answered by hit-testing.</summary>
public abstract class ChromelessForm : Form
{
    private const int WmNcCalcSize = 0x0083;

    /// <summary>The height of the strip at the top of the client area that drags the window.</summary>
    protected abstract int CaptionHeight { get; }

    /// <summary>The window size for a client size: the native side and bottom borders, no title bar. Size the window with
    /// it; the ClientSize setter measures with the system title bar.</summary>
    protected override Size SizeFromClientSize(Size clientSize)
    {
        var framed = base.SizeFromClientSize(clientSize);
        var border = (framed.Width - clientSize.Width) / 2;
        return new Size(framed.Width, clientSize.Height + border);
    }

    protected override void WndProc(ref Message m)
    {
        switch (m.Msg)
        {
            case WmNcCalcSize when m.WParam != IntPtr.Zero:
                CalcClientWithoutTitleBar(ref m);
                return;
            case NcHitTest.WindowMessage:
                base.WndProc(ref m);
                if ((int)m.Result == NcHitTest.Client) m.Result = HitTestClient(PointToClient(NcHitTest.ScreenPoint(m)));
                return;
            default:
                base.WndProc(ref m);
                return;
        }
    }

    /// <summary>Lets Windows place the borders, then gives the title bar's height back to the client area. A maximized
    /// window hangs its frame off the screen on every side, so its client top moves down by the frame width.</summary>
    private void CalcClientWithoutTitleBar(ref Message m)
    {
        var window = Marshal.PtrToStructure<NcCalcSizeParams>(m.LParam).Window;
        base.WndProc(ref m);
        var parameters = Marshal.PtrToStructure<NcCalcSizeParams>(m.LParam);
        var frame = parameters.Window.Left - window.Left;
        parameters.Window.Top = window.Top + (IsZoomed(Handle) ? frame : 0);
        Marshal.StructureToPtr(parameters, m.LParam, false);
        m.Result = IntPtr.Zero;
    }

    private IntPtr HitTestClient(Point point)
    {
        if (!IsZoomed(Handle) && point.Y < Theme.ResizeBand)
        {
            if (point.X < Theme.ResizeBand) return NcHitTest.TopLeft;
            return point.X >= ClientSize.Width - Theme.ResizeBand ? NcHitTest.TopRight : NcHitTest.Top;
        }
        return point.Y < CaptionHeight ? NcHitTest.Caption : NcHitTest.Client;
    }

    /// <summary>Maximized right now (WindowState only follows once the resize is done).</summary>
    [DllImport("user32.dll")]
    private static extern bool IsZoomed(IntPtr hwnd);

    [StructLayout(LayoutKind.Sequential)]
    private struct Rect
    {
        public int Left, Top, Right, Bottom;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct NcCalcSizeParams
    {
        public Rect Window;
        public Rect Before;
        public Rect Client;
        public IntPtr Position;
    }
}
