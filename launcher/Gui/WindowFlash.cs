using System.Runtime.InteropServices;

namespace CoopLauncher.Gui;

/// <summary>Flashes a window's taskbar button until the window comes to the front (an invite arrived while it was behind).</summary>
internal static class WindowFlash
{
    private const uint FlashAll = 0x3;
    private const uint FlashUntilForeground = 0xC;

    [StructLayout(LayoutKind.Sequential)]
    private struct FlashInfo
    {
        public uint Size;
        public IntPtr Window;
        public uint Flags;
        public uint Count;
        public uint TimeoutMs;
    }

    public static void Start(Form form)
    {
        var info = new FlashInfo { Size = (uint)Marshal.SizeOf<FlashInfo>(), Window = form.Handle, Flags = FlashAll | FlashUntilForeground };
        FlashWindowEx(ref info);
    }

    [DllImport("user32.dll")]
    private static extern bool FlashWindowEx(ref FlashInfo info);
}
