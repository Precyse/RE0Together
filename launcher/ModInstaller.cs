using System.Security.Cryptography;

namespace CoopLauncher;

public enum ModState { NotInstalled, Installed, UpdateAvailable }

/// <summary>A game's mod as found in its game folder: the state and the package build recorded when it was installed (0 if unknown).</summary>
public readonly record struct ModStatus(ModState State, int Build);

/// <summary>
/// Installs and removes a game's mod: the profile's adapter files in the game folder. A file the launcher installed has
/// an ownership marker beside it (<c>.cfown</c>, holding the installed hash); a file that was there before is kept as
/// <c>.cfbak</c> and restored on uninstall. The installed package build is recorded beside the first file (<c>.cfbuild</c>).
/// </summary>
public static class ModInstaller
{
    private const string BackupSuffix = ".cfbak";
    private const string OwnershipSuffix = ".cfown";
    private const string BuildSuffix = ".cfbuild";

    private readonly record struct ModFile(string Src, string Dst);

    /// <summary>Installed when every adapter file carries the marker; an update is available when a package copy differs from the installed one.</summary>
    public static ModStatus Status(GameProfile profile, string? gameDir)
    {
        if (gameDir == null) return new ModStatus(ModState.NotInstalled, 0);
        var files = Files(profile, gameDir);
        if (files.Count == 0 || !files.All(f => File.Exists(f.Dst) && File.Exists(f.Dst + OwnershipSuffix)))
            return new ModStatus(ModState.NotInstalled, 0);
        try
        {
            var stale = files.Any(f => File.Exists(f.Src) && Hash(f.Src) != Hash(f.Dst));
            return new ModStatus(stale ? ModState.UpdateAvailable : ModState.Installed, ReadBuild(files[0].Dst));
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            Log.Info($"Could not read the {profile.Name} mod: {e.Message}");
            return new ModStatus(ModState.NotInstalled, 0);
        }
    }

    /// <summary>Copies the package's adapter files into the game folder (also how an update re-copies them).</summary>
    public static bool Install(GameProfile profile, string gameDir)
    {
        if (GameLauncher.IsRunning(profile))
        {
            Log.Info($"Close {profile.Name} before installing its mod");
            return false;
        }
        var files = Files(profile, gameDir);
        var missing = files.Where(f => !File.Exists(f.Src)).Select(f => f.Src).FirstOrDefault();
        if (missing != null)
        {
            Log.Info($"Mod files missing from the package: {missing}");
            return false;
        }
        try
        {
            foreach (var file in files) Copy(file);
            File.WriteAllText(files[0].Dst + BuildSuffix, BuildCheck.LocalBuild().ToString());
            Log.Info($"Installed the {profile.Name} mod");
            return true;
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            Log.Info($"Could not install the {profile.Name} mod: {e.Message}");
            return false;
        }
    }

    /// <summary>Removes the adapter files the launcher installed and puts back any original it backed up.</summary>
    public static bool Uninstall(GameProfile profile, string gameDir)
    {
        if (GameLauncher.IsRunning(profile))
        {
            Log.Info($"Close {profile.Name} before removing its mod");
            return false;
        }
        try
        {
            var files = Files(profile, gameDir);
            foreach (var file in files)
            {
                var backup = file.Dst + BackupSuffix;
                if (File.Exists(backup)) File.Move(backup, file.Dst, overwrite: true);
                else File.Delete(file.Dst);
                File.Delete(file.Dst + OwnershipSuffix);
            }
            File.Delete(files[0].Dst + BuildSuffix);
            Log.Info($"Removed the {profile.Name} mod");
            return true;
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            Log.Info($"Could not remove the {profile.Name} mod: {e.Message}");
            return false;
        }
    }

    private static List<ModFile> Files(GameProfile profile, string gameDir) =>
        profile.AdapterFiles.Select(f => new ModFile(Path.Combine(RepoPaths.RepoRoot, f.Src), Path.Combine(gameDir, f.Dst))).ToList();

    private static void Copy(ModFile file)
    {
        var srcHash = Hash(file.Src);
        var marker = file.Dst + OwnershipSuffix;
        if (File.Exists(file.Dst))
        {
            var dstHash = Hash(file.Dst);
            var ours = File.Exists(marker);
            var backup = file.Dst + BackupSuffix;
            if (!ours && !File.Exists(backup))
            {
                File.Copy(file.Dst, backup);
                Log.Info($"Backed up existing {file.Dst} to {backup}");
            }
            if (dstHash == srcHash && ours) return;
        }
        File.Copy(file.Src, file.Dst, overwrite: true);
        File.WriteAllText(marker, srcHash);
    }

    private static int ReadBuild(string dst)
    {
        var path = dst + BuildSuffix;
        return File.Exists(path) && int.TryParse(File.ReadAllText(path).Trim(), out var build) ? build : 0;
    }

    private static string Hash(string path)
    {
        using var stream = File.OpenRead(path);
        return Convert.ToHexString(SHA256.HashData(stream));
    }
}
