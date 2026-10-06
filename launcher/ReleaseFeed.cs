using System.Text.Json;
using System.Text.Json.Serialization;

namespace CoopLauncher;

/// <summary>
/// Reads the rolling GitHub release `latest` (repo from update.json). One shared read: the result is cached, so the
/// unauthenticated GitHub API is hit once at startup and again only when the Update button forces it. A failed read
/// is logged and leaves the previous result in place.
/// </summary>
public static class ReleaseFeed
{
    public const string AssetName = "RE0-Coop.zip";
    private const string ConfigFile = "update.json";
    private const string PlaceholderRepo = "OWNER/REPO";
    private const string ReleaseUrlFormat = "https://api.github.com/repos/{0}/releases/tags/latest";
    private const string UserAgent = "coop-launcher-updater";
    private const string BuildNamePrefix = "Build ";
    private const int MetadataTimeoutSeconds = 5;

    private static readonly HttpClient Client = CreateClient();
    private static readonly JsonSerializerOptions JsonOptions = new() { PropertyNameCaseInsensitive = true };
    private static readonly object Gate = new();
    private static Release? _cached;
    private static bool _read;

    private sealed record UpdateConfig(string? Repo);

    public sealed record ReleaseAsset(string Name, [property: JsonPropertyName("browser_download_url")] string DownloadUrl);

    public sealed record Release(string? Name, List<ReleaseAsset>? Assets)
    {
        public int? Build => Name is { } name && name.StartsWith(BuildNamePrefix) && int.TryParse(name[BuildNamePrefix.Length..], out var build) ? build : null;

        public string? ZipUrl => Assets?.FirstOrDefault(a => a.Name == AssetName)?.DownloadUrl;
    }

    /// <summary>The release, from the cache once read; `force` reads GitHub again. Null when it never could be read.</summary>
    public static Release? Read(bool force = false)
    {
        lock (Gate)
        {
            if (_read && !force) return _cached;
            _read = true;
            try
            {
                if (ReadRepo() is { } repo) _cached = Fetch(repo) ?? _cached;
            }
            catch (Exception e)
            {
                Log.Info($"Release check failed: {e.Message}");
            }
            return _cached;
        }
    }

    internal static HttpResponseMessage Get(string url, CancellationToken cancel)
    {
        using var request = new HttpRequestMessage(HttpMethod.Get, url);
        var response = Client.Send(request, HttpCompletionOption.ResponseHeadersRead, cancel);
        return response.EnsureSuccessStatusCode();
    }

    private static Release? Fetch(string repo)
    {
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(MetadataTimeoutSeconds));
        using var response = Get(string.Format(ReleaseUrlFormat, repo), timeout.Token);
        using var body = response.Content.ReadAsStream(timeout.Token);
        return JsonSerializer.Deserialize<Release>(body, JsonOptions);
    }

    private static string? ReadRepo()
    {
        var path = Path.Combine(AppContext.BaseDirectory, ConfigFile);
        if (!File.Exists(path)) return null;
        var repo = JsonSerializer.Deserialize<UpdateConfig>(File.ReadAllText(path), JsonOptions)?.Repo;
        return string.IsNullOrWhiteSpace(repo) || repo == PlaceholderRepo ? null : repo;
    }

    private static HttpClient CreateClient()
    {
        var client = new HttpClient { Timeout = Timeout.InfiniteTimeSpan };
        client.DefaultRequestHeaders.UserAgent.ParseAdd(UserAgent);
        return client;
    }
}
