using System.Diagnostics;

namespace CoopLauncher;

/// <summary>Ends a game this launcher started only for itself (the capture launch) or that the player asked to close: asked to close
/// once, then ended; never waited for, so the loop thread keeps running while the game goes.</summary>
public static class GameCloser
{
    /// <summary>Asks every process of the game to close its main window.</summary>
    public static void RequestClose(GameProfile profile)
    {
        foreach (var process in ProcessesOf(profile))
        {
            using (process)
            {
                if (!process.HasExited) process.CloseMainWindow();
            }
        }
    }

    /// <summary>Ends every process of the game that is still there.</summary>
    public static void Kill(GameProfile profile)
    {
        foreach (var process in ProcessesOf(profile))
        {
            using (process)
            {
                if (!process.HasExited) process.Kill();
            }
        }
    }

    public static bool IsGone(GameProfile profile) => !GameLauncher.IsRunning(profile);

    private static Process[] ProcessesOf(GameProfile profile) => Process.GetProcessesByName(Path.GetFileNameWithoutExtension(profile.Exe));
}
