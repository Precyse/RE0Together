using System.Runtime.InteropServices;

namespace CoopLauncher;

/// <summary>The launcher is a windowed program, so no console window flashes up at start. A command-line start attaches
/// to the console it was started from so its log lines show there; redirected output is unaffected.</summary>
internal static class ParentConsole
{
    private const int AttachParentProcess = -1;

    [DllImport("kernel32.dll")]
    private static extern bool AttachConsole(int processId);

    public static void Attach() => AttachConsole(AttachParentProcess);
}
