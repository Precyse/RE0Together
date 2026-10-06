using System.Diagnostics;
using System.IO.Compression;

namespace CoopLauncher;

/// <summary>
/// Installs a newer build from the rolling GitHub release `latest` (read through ReleaseFeed), then relaunches the
/// launcher. The package is replaced whole: launcher, every game profile and every adapter. Only runs from a packaged
/// layout (root\launcher\app\coop-launcher.exe) and only ever writes below that root. Any failure means "no update".
/// </summary>
public static class Updater
{
    private const string AppDirName = "app";
    private const string LauncherDirName = "launcher";
    private const string OldSuffix = ".old";
    private const string WorkDirPrefix = "coop-launcher-update-";
    private const string ExtractedDirName = "files";
    private const int DownloadTimeoutSeconds = 300;

    /// <summary>True when a newer build was installed and started; the caller must exit. Always reads the release afresh.</summary>
    public static bool TryInstall(string[] args)
    {
        if (FindPackageRoot() is not { } root) return false;
        DeleteOldFiles(root);
        try
        {
            if (ReleaseFeed.Read(force: true) is not { Build: { } build, ZipUrl: { } zipUrl }) return false;
            if (build <= BuildCheck.LocalBuild())
            {
                Log.Info($"Up to date (build {BuildCheck.LocalBuild()})");
                return false;
            }
            Log.Info($"Installing build {build}");
            InstallFrom(zipUrl, root);
            if (BuildCheck.LocalBuild() < build) throw new InvalidDataException($"package did not update {BuildCheck.VersionFile}");
            Relaunch(args);
            return true;
        }
        catch (Exception e)
        {
            Log.Info($"Update skipped: {e.Message}");
            return false;
        }
    }

    /// <summary>The folder two levels above the exe, or null when not running from the packaged layout.</summary>
    private static string? FindPackageRoot()
    {
        var appDir = new DirectoryInfo(AppContext.BaseDirectory);
        var launcherDir = appDir.Parent;
        return appDir.Name == AppDirName && launcherDir?.Name == LauncherDirName ? launcherDir.Parent?.FullName : null;
    }

    private static void InstallFrom(string zipUrl, string root)
    {
        var workDir = Path.Combine(Path.GetTempPath(), WorkDirPrefix + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(workDir);
        try
        {
            var zipPath = Path.Combine(workDir, ReleaseFeed.AssetName);
            Download(zipUrl, zipPath);
            var extracted = Path.Combine(workDir, ExtractedDirName);
            ZipFile.ExtractToDirectory(zipPath, extracted);
            Replace(extracted, root);
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

    /// <summary>Running files cannot be overwritten but can be renamed, so every existing file moves aside to *.old first.</summary>
    private static void Replace(string source, string root)
    {
        foreach (var file in Directory.EnumerateFiles(source, "*", SearchOption.AllDirectories))
        {
            var target = Path.Combine(root, Path.GetRelativePath(source, file));
            Directory.CreateDirectory(Path.GetDirectoryName(target)!);
            if (File.Exists(target)) File.Move(target, target + OldSuffix, overwrite: true);
            File.Copy(file, target);
        }
    }

    private static void Relaunch(string[] args)
    {
        var start = new ProcessStartInfo(Environment.ProcessPath!) { UseShellExecute = false };
        foreach (var arg in args) start.ArgumentList.Add(arg);
        Process.Start(start);
    }

    private static void DeleteOldFiles(string root)
    {
        foreach (var file in Directory.EnumerateFiles(root, "*" + OldSuffix, SearchOption.AllDirectories)) Quietly(() => File.Delete(file));
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
