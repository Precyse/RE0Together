using System.ComponentModel;
using System.Diagnostics;

namespace CoopLauncher;

/// <summary>One window launcher at a time: a stale copy still holding the Steam session makes joining silently fail.</summary>
public static class InstanceGuard
{
    private const int ExitWaitMs = 3000;

    /// <summary>Closes every other process of this executable (older windows, a dead copy left behind).</summary>
    public static void CloseOtherLaunchers()
    {
        using var self = Process.GetCurrentProcess();
        foreach (var other in Process.GetProcessesByName(self.ProcessName))
        {
            using (other)
            {
                if (other.Id == self.Id) continue;
                try
                {
                    other.Kill();
                    other.WaitForExit(ExitWaitMs);
                    Log.Info($"Closed an older launcher (process {other.Id})");
                }
                catch (Exception e) when (e is InvalidOperationException or Win32Exception)
                {
                    Log.Info($"Could not close launcher process {other.Id}: {e.Message}");
                }
            }
        }
    }
}
