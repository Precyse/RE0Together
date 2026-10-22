using System.Runtime.InteropServices;
using Microsoft.Win32;

namespace CoopLauncher.Gui;

/// <summary>Whether Windows' "Animation effects" setting is off; the page then plays no animation. Read again after Windows
/// reports a changed user preference.</summary>
public static class ReducedMotion
{
    private const uint GetClientAreaAnimation = 0x1042;

    private static volatile bool _stale = true;
    private static bool _reduced;

    static ReducedMotion()
    {
        SystemEvents.UserPreferenceChanged += (_, _) => _stale = true;
    }

    /// <summary>Forces reduced motion on or off (tests); null follows Windows.</summary>
    public static bool? Override { get; set; }

    public static bool Reduced => Override ?? SystemReduced();

    private static bool SystemReduced()
    {
        if (!_stale) return _reduced;
        _stale = false;
        var enabled = 1;
        _reduced = SystemParametersInfo(GetClientAreaAnimation, 0, ref enabled, 0) && enabled == 0;
        return _reduced;
    }

    [DllImport("user32.dll")]
    private static extern bool SystemParametersInfo(uint action, uint param, ref int value, uint winIni);
}
