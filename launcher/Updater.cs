using System.Diagnostics;
using System.Security.Cryptography;
using System.Text;

namespace CoopLauncher;

/// <summary>
/// Installs a newer release of the channel (the manifest read through ReleaseFeed) into the packaged layout. Each component has its own
/// version, and a release is newer when one of them differs from installed.json. Only the components
/// with a file that differs from the manifest are downloaded (<see cref="ComponentUpdate"/>), and their changed files are
/// replaced in one transaction; a mods-only update therefore costs the adapter, not the launcher, and needs no restart. A command-line
/// run relaunches at start-up when the launcher changed; the window stages in the background and relaunches when the player asks
/// (<see cref="RestartRequested"/>). A replace that fails, or is cut short, puts every original back, and the versions in
/// installed.json are written last. Only runs from a packaged layout (root\launcher\app\coop-launcher.exe) and only ever writes below
/// that root. Any failure means "no update".
/// </summary>
public static class Updater
{
    private const string OldSuffix = ".old";
    private const string PendingMarkerName = "update.pending";
    private const string WorkDirPrefix = "coop-launcher-update-";
    private const int DownloadTimeoutSeconds = 300;
    private const string LockNamePrefix = @"Local\CoopLauncherUpdate-";
    private const int LockNameHashChars = 16;

    /// <summary>True after an update that changes the running launcher was installed in this process: the files on disk are newer
    /// than the running copy. A mods-only update leaves it false, since it applies at once.</summary>
    public static bool Staged { get; private set; }

    /// <summary>Set by the window when the player asks for the restart; Program relaunches the staged build once the window is closed.</summary>
    public static bool RestartRequested { get; set; }

    /// <summary>Start-up of a command-line run: true when an update that changes the launcher was installed and started; the caller
    /// must exit. Safe because no Steam session exists yet in this process.</summary>
    public static bool TryInstall(string[] args) => Stage() && Staged && TryRelaunch(args);

    /// <summary>Starts the staged build after the window closed (no arguments, the predecessor handoff).</summary>
    public static void RelaunchStaged() => TryRelaunch([]);

    /// <summary>Downloads and installs a newer release without starting it. True when one was installed; when it changes the
    /// launcher (<see cref="Staged"/>) the running copy keeps going until the user reopens it. The download and the install are
    /// reported to <paramref name="progress"/>.</summary>
    public static bool Stage(IProgress<ModProgress>? progress = null)
    {
        var report = new ProgressReporter(progress);
        if (FindPackageRoot() is not { } root)
        {
            Log.Info("Updates install only from the packaged launcher");
            return false;
        }
        using var gate = TryAcquireLock(root);
        if (gate == null)
        {
            Log.Info("Another launcher is updating, skipped");
            return false;
        }
        try
        {
            Recover(root);
            if (ReleaseFeed.Read(force: true) is not { } release)
            {
                Log.Info("No release to update from");
                return false;
            }
            if (!release.UpdateAvailable)
            {
                Log.Info($"Up to date (launcher v{BuildCheck.LocalBuild()})");
                return false;
            }
            Staged |= Apply(release.Manifest, release.Assets, root, report);
            report.Finish();
            return true;
        }
        catch (Exception e)
        {
            Log.Info($"Update skipped: {e.Message}");
            report.Fail(e.Message);
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

    /// <summary>Takes the update lock of a package, or null when another process (a window staging, a command-line start) holds it.
    /// The lock keeps a start-up <see cref="Recover"/> from rolling back a replace that is under way in another process. Dispose
    /// releases it, on the thread that took it.</summary>
    internal static IDisposable? TryAcquireLock(string root)
    {
        var digest = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(Path.GetFullPath(root).ToLowerInvariant())));
        var mutex = new Mutex(initiallyOwned: false, LockNamePrefix + digest[..LockNameHashChars]);
        try
        {
            if (mutex.WaitOne(0)) return new LockHandle(mutex);
        }
        catch (AbandonedMutexException)
        {
            return new LockHandle(mutex);
        }
        mutex.Dispose();
        return null;
    }

    /// <summary>The folder two levels above the exe, or null when not running from the packaged layout.</summary>
    private static string? FindPackageRoot()
    {
        var appDir = new DirectoryInfo(AppContext.BaseDirectory);
        var launcherDir = appDir.Parent;
        return appDir.Name == InstalledRelease.AppDirName && launcherDir?.Name == InstalledRelease.LauncherDirName ? launcherDir.Parent?.FullName : null;
    }

    /// <summary>Brings the package at <paramref name="root"/> to the manifest: fetches what differs, replaces it in one transaction
    /// and records the component versions, with a log line for each one that changed. True when the launcher itself changed (it must
    /// be restarted). A manifest that matches the files already only records the versions.</summary>
    public static bool Apply(ReleaseManifest manifest, IAssetSource source, string root, ProgressReporter report)
    {
        var plan = ComponentUpdate.Plan(manifest, root);
        var installedPath = Path.Combine(root, InstalledRelease.LauncherDirName, InstalledRelease.FileName);
        var before = InstalledRelease.Read(installedPath);
        if (!plan.IsEmpty)
        {
            var workDir = Path.Combine(Path.GetTempPath(), WorkDirPrefix + Guid.NewGuid().ToString("N"));
            try
            {
                using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(DownloadTimeoutSeconds));
                var staged = ComponentUpdate.Stage(plan, source, workDir, report, timeout.Token);
                report.Begin(ModStep.Installing, 1, 1);
                Install(staged, plan, root, manifest.Build);
            }
            finally
            {
                Quietly(() => Directory.Delete(workDir, recursive: true));
            }
        }
        foreach (var component in before.Changed(manifest))
            Log.Info(VersionLines.Updated(VersionLines.SubjectOf(component.Id), component.Version, before.VersionOf(component.Id)));
        new InstalledRelease(manifest.Build, manifest.Channel, manifest.Commit, manifest.Components.ToDictionary(component => component.Id, component => component.Version))
            .Write(installedPath);
        return plan.RestartNeeded;
    }

    /// <summary>Replaces the changed files and checks them against the manifest. The pending marker (the target build) stays until
    /// the files are confirmed; a start that finds it knows the replace was cut short (<see cref="Recover"/>).</summary>
    private static void Install(string staged, UpdatePlan plan, string root, int build)
    {
        var marker = Path.Combine(root, PendingMarkerName);
        File.WriteAllText(marker, build.ToString());
        Replace(staged, root);
        var wrong = plan.Files.FirstOrDefault(entry => !ComponentUpdate.Matches(entry.File, Path.Combine(root, entry.File.Path)));
        if (wrong.File != null)
        {
            RestoreBackups(root);
            throw new InvalidDataException($"{wrong.File.Path} is not as the manifest has it after the copy");
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

    private sealed class LockHandle : IDisposable
    {
        private readonly Mutex _mutex;

        public LockHandle(Mutex mutex) => _mutex = mutex;

        public void Dispose()
        {
            _mutex.ReleaseMutex();
            _mutex.Dispose();
        }
    }
}
