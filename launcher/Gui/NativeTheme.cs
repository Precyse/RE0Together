using System.Runtime.InteropServices;

namespace CoopLauncher.Gui;

/// <summary>Windows' own dark parts: the window frame (dark mode, a border in the line colour), and the scrollbars of
/// native controls.</summary>
internal static class NativeTheme
{
    private const int DwmUseImmersiveDarkMode = 20;
    private const int DwmBorderColor = 34;
    private const string DarkScrollbarTheme = "DarkMode_Explorer";

    public static void DarkFrame(Form form)
    {
        var dark = 1;
        DwmSetWindowAttribute(form.Handle, DwmUseImmersiveDarkMode, ref dark, sizeof(int));
        var border = ColorTranslator.ToWin32(Theme.Line);
        DwmSetWindowAttribute(form.Handle, DwmBorderColor, ref border, sizeof(int));
    }

    /// <summary>Dark scrollbars on the control, now and whenever its handle is recreated.</summary>
    public static void DarkScrollbars(Control control)
    {
        control.HandleCreated += (_, _) => SetWindowTheme(control.Handle, DarkScrollbarTheme, null);
        if (control.IsHandleCreated) SetWindowTheme(control.Handle, DarkScrollbarTheme, null);
    }

    [DllImport("dwmapi.dll")]
    private static extern int DwmSetWindowAttribute(IntPtr hwnd, int attribute, ref int value, int size);

    [DllImport("uxtheme.dll", CharSet = CharSet.Unicode)]
    private static extern int SetWindowTheme(IntPtr hwnd, string subAppName, string? subIdList);
}
