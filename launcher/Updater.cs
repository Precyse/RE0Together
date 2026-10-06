using System.Diagnostics;
using System.IO.Compression;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace CoopLauncher;

/// <summary>
/// Installs a newer build from the rolling GitHub release `latest`, then relaunches the launcher. Only runs from a packaged
/// layout (root\launcher\app\coop-launcher.exe) and only ever writes below that root. Any failure means "no update".
/// </summary>
public static class Updater
{
    private const string ConfigFile = "update.json";
    private const string PlaceholderRepo = "OWNER/REPO";
    private const string ReleaseUrlFormat = "https://api.github.com/repos/{0}/releases/tags/latest";
    private const string UserAgent = "coop-launcher-updater";
    private const string AppDirName = "app";
    private const string LauncherDirName = "launcher";
    private const string OldSuffix = ".old";
    private const string BuildNamePrefix = "Build ";
    private const string AssetName = "RE0-Coop.zip";
    private const string WorkDirPrefix = "coop-launcher-update-";
    private const string ExtractedDirName = "files";
    private const int MetadataTimeoutSeconds = 5;
    private const int DownloadTimeoutSeconds = 300;

    private static readonly HttpClient Client = CreateClient();
    private static readonly JsonSerializerOptions JsonOptions = new() { PropertyNameCaseInsensitive = true };

    private sealed record UpdateConfig(string? Repo);

    private sealed record ReleaseAsset(string Name, [property: JsonPropertyName("browser_download_url")] string DownloadUrl);

    private sealed record Release(string? Name, List<ReleaseAsset>? Assets)
    {
        public int? Build => Name is { } name && name.StartsWith(BuildNamePrefix) && int.TryParse(name[BuildNamePrefix.Length..], out var build) ? build : null;

        public string? ZipUrl => Assets?.FirstOrDefault(a => a.Name == AssetName)?.DownloadUrl;
    }

    /// <summary>True when a newer build was installed and started; the caller must exit.</summary>
    public static bool TryInstall(string[] args)
    {
        if (FindPackageRoot() is not { } root) return false;
        DeleteOldFiles(root);
        try
        {
            var release = ReadRepo() is { } repo ? FetchRelease(repo) : null;
            if (release is not { Build: { } build, ZipUrl: { } zipUrl }) return false;
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

    private static string? ReadRepo()
    {
        var path = Path.Combine(AppContext.BaseDirectory, ConfigFile);
        if (!File.Exists(path)) return null;
        var repo = JsonSerializer.Deserialize<UpdateConfig>(File.ReadAllText(path), JsonOptions)?.Repo;
        return string.IsNullOrWhiteSpace(repo) || repo == PlaceholderRepo ? null : repo;
    }


    private static Release? FetchRelease(string repo)
    {
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(MetadataTimeoutSeconds));
        using var response = Get(string.Format(ReleaseUrlFormat, repo), timeout.Token);
        using var body = response.Content.ReadAsStream(timeout.Token);
        return JsonSerializer.Deserialize<Release>(body, JsonOptions);
    }

    private static void InstallFrom(string zipUrl, string root)
    {
        var workDir = Path.Combine(Path.GetTempPath(), WorkDirPrefix + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(workDir);
        try
        {
            var zipPath = Path.Combine(workDir, AssetName);
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
        using var response = Get(url, timeout.Token);
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

    private static HttpResponseMessage Get(string url, CancellationToken cancel)
    {
        using var request = new HttpRequestMessage(HttpMethod.Get, url);
        var response = Client.Send(request, HttpCompletionOption.ResponseHeadersRead, cancel);
        return response.EnsureSuccessStatusCode();
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

    private static HttpClient CreateClient()
    {
        var client = new HttpClient { Timeout = Timeout.InfiniteTimeSpan };
        client.DefaultRequestHeaders.UserAgent.ParseAdd(UserAgent);
        return client;
    }
}
