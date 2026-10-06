using System.Diagnostics;

namespace CoopLauncher;

/// <summary>Starts a game through Steam. The mod is installed separately (ModInstaller); a launch never installs it.</summary>
public static class GameLauncher
{
    public static bool IsRunning(GameProfile profile) =>
        Process.GetProcessesByName(Path.GetFileNameWithoutExtension(profile.Exe)).Length > 0;

    /// <summary>False when the game's mod is not installed, the installed game build is not supported or Steam could not start the game, so the session cannot go on.</summary>
    public static bool Launch(GameProfile profile, string? gameDir)
    {
        var build = GameBuilds.Installed(profile);
        if (!GameBuilds.IsSupported(profile, build))
        {
            Log.Info(GameBuilds.Describe(profile, build));
            return false;
        }
        if (IsRunning(profile))
        {
            Log.Info($"{profile.Name} is already running, not starting it again; if it was started outside the launcher, restart it from here");
            return true;
        }
        if (ModInstaller.Status(profile, gameDir).State == ModState.NotInstalled)
        {
            Log.Info($"The {profile.Name} mod is not installed, install it from the launcher first");
            return false;
        }
        Log.Info($"Starting {profile.Name} via Steam");
        try
        {
            Process.Start(new ProcessStartInfo($"steam://rungameid/{profile.SteamAppId}") { UseShellExecute = true });
            return true;
        }
        catch (System.ComponentModel.Win32Exception e)
        {
            Log.Info($"Could not start {profile.Name} through Steam: {e.Message}");
            return false;
        }
    }
}
