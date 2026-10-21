using System.Diagnostics;

namespace CoopLauncher;

/// <summary>How a game start ended: started, already running (left alone), or why it could not start.</summary>
public enum LaunchResult { Started, AlreadyRunning, GameUnsupported, ModNotInstalled, SteamStartFailed }

/// <summary>Starts a game through Steam. The mod is installed separately (ModInstaller); a launch never installs it.</summary>
public static class GameLauncher
{
    public static bool IsRunning(GameProfile profile)
    {
        var processes = Process.GetProcessesByName(Path.GetFileNameWithoutExtension(profile.Exe));
        foreach (var process in processes) process.Dispose();
        return processes.Length > 0;
    }

    /// <summary>Starts a game that matches its players itself: the options (<see cref="GameOptions.ForAdapter"/>) go into the adapter ini first.</summary>
    public static LaunchResult LaunchWithOptions(GameProfile profile, string gameDir, IReadOnlyDictionary<string, string> options)
    {
        try
        {
            AdapterSettings.WriteOptions(gameDir, profile, options);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            Log.Info($"Could not write the {profile.Name} options: {e.Message}");
            return LaunchResult.SteamStartFailed;
        }
        return Launch(profile, gameDir);
    }

    /// <summary>Anything but <see cref="LaunchResult.Started"/> or <see cref="LaunchResult.AlreadyRunning"/> means the
    /// session cannot go on: the mod is not installed, the installed game build is not supported or Steam could not start the game.</summary>
    public static LaunchResult Launch(GameProfile profile, string? gameDir)
    {
        var build = GameBuilds.Installed(profile, gameDir);
        if (!GameBuilds.IsSupported(profile, build))
        {
            Log.Info(GameBuilds.Describe(profile, build));
            return LaunchResult.GameUnsupported;
        }
        if (IsRunning(profile))
        {
            Log.Info($"{profile.Name} is already running, not starting it again; if it was started outside the launcher, restart it from here");
            return LaunchResult.AlreadyRunning;
        }
        if (ModInstaller.Status(profile, gameDir).State == ModState.NotInstalled)
        {
            Log.Info($"The {profile.Name} mod is not installed, install it from the launcher first");
            return LaunchResult.ModNotInstalled;
        }
        Log.Info($"Starting {profile.Name} via Steam");
        try
        {
            Process.Start(new ProcessStartInfo($"steam://rungameid/{profile.SteamAppId}") { UseShellExecute = true });
            return LaunchResult.Started;
        }
        catch (System.ComponentModel.Win32Exception e)
        {
            Log.Info($"Could not start {profile.Name} through Steam: {e.Message}");
            return LaunchResult.SteamStartFailed;
        }
    }
}
