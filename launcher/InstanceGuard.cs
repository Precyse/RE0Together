using System.ComponentModel;
using System.Diagnostics;

namespace CoopLauncher;

/// <summary>
/// Clears out launcher processes that are not the running window (SingleInstance calls it when this start becomes the window
/// or when the window it found does not answer): a stale copy still holding the Steam session makes joining silently fail.
/// It never closes a live window and never a command-line run (those hold <see cref="CliMutexName"/>).
/// </summary>
public static class InstanceGuard
{
    public const string CliMutexPrefix = @"Local\CoopLauncher.Cli.";
    private const int ExitWaitMs = 3000;

    /// <summary>The mutex a command-line run holds for its whole life, so no start closes it.</summary>
    public static string CliMutexName(int processId) => CliMutexPrefix + processId;

    /// <summary>Whether the process is a command-line run (it holds its <see cref="CliMutexName"/>).</summary>
    public static bool IsCliRun(int processId)
    {
        if (!Mutex.TryOpenExisting(CliMutexName(processId), out var mutex)) return false;
        mutex.Dispose();
        return true;
    }

    /// <summary>Whether another launcher process may be closed. Never a command-line run, never the window that answers; any
    /// other process only when it is hung (not responding, or without a window).</summary>
    public static bool ShouldClose(bool isCli, bool holdsWindow, bool answered, bool responding) =>
        !isCli && !(holdsWindow && answered) && !responding;

    /// <summary>Closes the other launcher processes <see cref="ShouldClose"/> allows and returns how many it closed.
    /// <paramref name="holdsWindow"/>: the window mutex is held by another process, which may be among them.</summary>
    public static int CloseOtherLaunchers(bool holdsWindow, bool answered)
    {
        var closed = 0;
        using var self = Process.GetCurrentProcess();
        foreach (var other in Process.GetProcessesByName(self.ProcessName))
        {
            using (other)
            {
                if (other.Id == self.Id || !ShouldClose(IsCliRun(other.Id), holdsWindow, answered, IsResponding(other))) continue;
                try
                {
                    other.Kill();
                    other.WaitForExit(ExitWaitMs);
                    closed++;
                    Log.Info($"Closed a hung launcher (process {other.Id})");
                }
                catch (Exception e) when (e is InvalidOperationException or Win32Exception)
                {
                    Log.Info($"Could not close launcher process {other.Id}: {e.Message}");
                }
            }
        }
        return closed;
    }

    private static bool IsResponding(Process process)
    {
        try
        {
            return process.MainWindowHandle != IntPtr.Zero && process.Responding;
        }
        catch (Exception e) when (e is InvalidOperationException or Win32Exception)
        {
            return true;
        }
    }
}
