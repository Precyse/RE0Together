using System.Diagnostics;
using System.IO.Compression;

namespace CoopLauncher;

/// <summary>
/// Installs a newer build from the rolling GitHub release `latest` (read through ReleaseFeed). A command-line run relaunches at
/// start-up; the window stages in the background and relaunches when the player asks (<see cref="RestartRequested"/>). The
/// package is replaced whole: launcher, every game profile and every adapter. A replace that fails, or is cut short, puts every
/// original back. Only runs from a packaged layout (root\launcher\app\coop-launcher.exe) and only ever writes below that root.
/// Any failure means "no update".
/// </summary>
public static class Updater
{
    private const string AppDirName = "app";
    private const string LauncherDirName = "launcher";
    private const string OldSuffix = ".old";
    private const string PendingMarkerName = "update.pending";
    private const string WorkDirPrefix = "coop-launcher-update-";
    private const string ExtractedDirName = "files";
    private const int DownloadTimeoutSeconds = 300;

    /// <summary>True after a newer package was installed in this process: the files on disk are newer than the running copy.</summary>
    public static bool Staged { get; private set; }

    /// <summary>Set by the window when the player asks for the restart; Program relaunches the staged build once the window is closed.</summary>
    public static bool RestartRequested { get; set; }

    /// <summary>Start-up of a command-line run: true when a newer build was installed and started; the caller must exit. Safe
    /// because no Steam session exists yet in this process.</summary>
    public static bool TryInstall(string[] args) => Stage() && TryRelaunch(args);

    /// <summary>Starts the staged build after the window closed (no arguments, the predecessor handoff).</summary>
    public static void RelaunchStaged() => TryRelaunch([]);

    /// <summary>Downloads and installs a newer build without starting it. True when one was installed; the running copy
    /// keeps going until the user reopens the launcher.</summary>
    public static bool Stage()
    {
        if (FindPackageRoot() is not { } root)
        {
            Log.Info("Updates install only from the packaged launcher");
            return false;
        }
        Recover(root);
        try
        {
            if (ReleaseFeed.Read(force: true) is not { Build: { } build, ZipUrl: { } zipUrl })
            {
                Log.Info("No release to update from");
                return false;
            }
            if (build <= BuildCheck.LocalBuild())
            {
                Log.Info($"Up to date (build {BuildCheck.LocalBuild()})");
                return false;
            }
            Log.Info($"Installing build {build}");
            InstallFrom(zipUrl, root, build);
            Staged = true;
            return true;
        }
        catch (Exception e)
        {
            Log.Info($"Update skipped: {e.Message}");
            return false;
        }
    }

    /// <summary>Copies the package over <paramref name="root"/>. Running files cannot be overwritten but can be renamed, so every
    /// existing file moves aside to *.old first. Any failure puts every moved file back, deletes the copies made so far and rethrows.</summary>
    public static void Replace(string source, string root)
    {
        var moved = new List<string>();
        var copied = new List<string>();
        try
        {
            foreach (var file in Directory.EnumerateFiles(source, "*", SearchOption.AllDirectories))
            {
                var target = Path.Combine(root, Path.GetRelativePath(source, file));
                Directory.CreateDirectory(Path.GetDirectoryName(target)!);
                if (File.Exists(target))
                {
                    File.Move(target, target + OldSuffix, overwrite: true);
                    moved.Add(target);
                }
                copied.Add(target);
                File.Copy(file, target);
            }
        }
        catch
        {
            foreach (var target in copied) Quietly(() => File.Delete(target));
            foreach (var target in moved) Quietly(() => File.Move(target + OldSuffix, target, overwrite: true));
            throw;
        }
    }

    /// <summary>Start-up housekeeping: a pending marker means the last replace was cut short, so the originals come back from
    /// their *.old copies; without one the *.old files are leftovers of a finished update and are deleted.</summary>
    public static void Recover(string root)
    {
        var marker = Path.Combine(root, PendingMarkerName);
        if (File.Exists(marker))
        {
            Log.Info("An earlier update was interrupted, restoring the previous files");
            RestoreBackups(root);
            Quietly(() => File.Delete(marker));
            return;
        }
        foreach (var file in Directory.EnumerateFiles(root, "*" + OldSuffix, SearchOption.AllDirectories)) Quietly(() => File.Delete(file));
    }

    /// <summary>The folder two levels above the exe, or null when not running from the packaged layout.</summary>
    private static string? FindPackageRoot()
    {
        var appDir = new DirectoryInfo(AppContext.BaseDirectory);
        var launcherDir = appDir.Parent;
        return appDir.Name == AppDirName && launcherDir?.Name == LauncherDirName ? launcherDir.Parent?.FullName : null;
    }

    private static void InstallFrom(string zipUrl, string root, int build)
    {
        var workDir = Path.Combine(Path.GetTempPath(), WorkDirPrefix + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(workDir);
        try
        {
            var zipPath = Path.Combine(workDir, ReleaseFeed.AssetName);
            Download(zipUrl, zipPath);
            var extracted = Path.Combine(workDir, ExtractedDirName);
            ZipFile.ExtractToDirectory(zipPath, extracted);
            Install(extracted, root, build);
        }
        finally
        {
            Quietly(() => Directory.Delete(workDir, recursive: true));
        }
    }

    private static void Download(string url, string destination)
    {
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(DownloadTimeoutSeconds));
        using var response = ReleaseFeed.Get(url, timeout.Token);
        using var body = response.Content.ReadAsStream(timeout.Token);
        using var file = File.Create(destination);
        body.CopyTo(file);
    }

    /// <summary>Replaces the package and checks the version it brought. The pending marker (the target build) stays until the
    /// package is confirmed; a start that finds it knows the replace was cut short (<see cref="Recover"/>).</summary>
    private static void Install(string extracted, string root, int build)
    {
        var marker = Path.Combine(root, PendingMarkerName);
        File.WriteAllText(marker, build.ToString());
        Replace(extracted, root);
        if (BuildCheck.LocalBuild() < build)
        {
            RestoreBackups(root);
            throw new InvalidDataException($"package did not update {BuildCheck.VersionFile}");
        }
        File.Delete(marker);
    }

    private static void RestoreBackups(string root)
    {
        foreach (var backup in Directory.EnumerateFiles(root, "*" + OldSuffix, SearchOption.AllDirectories))
            Quietly(() => File.Move(backup, backup[..^OldSuffix.Length], overwrite: true));
    }

    private static bool TryRelaunch(string[] args)
    {
        try
        {
            var start = new ProcessStartInfo(Environment.ProcessPath!) { UseShellExecute = false };
            start.Environment[SingleInstance.PredecessorVariable] = Environment.ProcessId.ToString();
            foreach (var arg in args) start.ArgumentList.Add(arg);
            Process.Start(start);
            return true;
        }
        catch (Exception e) when (e is InvalidOperationException or System.ComponentModel.Win32Exception)
        {
            Log.Info($"Update installed but not started: {e.Message}");
            return false;
        }
    }

    private static void Quietly(Action action)
    {
        try
        {
            action();
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
        }
    }
}
