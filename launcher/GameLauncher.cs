using System.Diagnostics;
using System.Security.Cryptography;

namespace CoopLauncher;

/// <summary>Installs the profile's adapter files into the game folder, then starts the game through Steam.</summary>
public static class GameLauncher
{
    private const string BackupSuffix = ".cfbak";
    private const string OwnershipSuffix = ".cfown";

    public static bool IsRunning(GameProfile profile) =>
        Process.GetProcessesByName(Path.GetFileNameWithoutExtension(profile.Exe)).Length > 0;

    public static void Launch(GameProfile profile, string? gameDir)
    {
        if (IsRunning(profile))
        {
            Log.Info($"{profile.Name} is already running, not starting it again");
            return;
        }
        if (gameDir == null) Log.Info($"Game folder for app {profile.SteamAppId} not found, skipping adapter install");
        else InstallAdapters(profile, gameDir);
        Log.Info($"Starting {profile.Name} via Steam");
        Process.Start(new ProcessStartInfo($"steam://rungameid/{profile.SteamAppId}") { UseShellExecute = true });
    }

    private static void InstallAdapters(GameProfile profile, string gameDir)
    {
        foreach (var file in profile.AdapterFiles) Install(Path.Combine(RepoPaths.RepoRoot, file.Src), Path.Combine(gameDir, file.Dst));
    }

    private static void Install(string src, string dst)
    {
        if (!File.Exists(src))
        {
            Log.Info($"Adapter not built yet, skipping install: {src}");
            return;
        }
        var srcHash = Hash(src);
        var ownerMarker = dst + OwnershipSuffix;
        if (File.Exists(dst))
        {
            var dstHash = Hash(dst);
            if (dstHash == srcHash) return;
            var ours = File.Exists(ownerMarker) && File.ReadAllText(ownerMarker) == dstHash;
            var backup = dst + BackupSuffix;
            if (!ours && !File.Exists(backup))
            {
                File.Copy(dst, backup);
                Log.Info($"Backed up existing {dst} to {backup}");
            }
        }
        File.Copy(src, dst, overwrite: true);
        File.WriteAllText(ownerMarker, srcHash);
        Log.Info($"Installed {dst}");
    }

    private static string Hash(string path)
    {
        using var stream = File.OpenRead(path);
        return Convert.ToHexString(SHA256.HashData(stream));
    }
}
