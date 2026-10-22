using System.Text;
using System.Text.Json;

namespace CoopLauncher;

/// <summary>
/// Reads the release channel's manifest (repo and channel from update.json): the file
/// <c>releases/download/{channel}/manifest.json</c>, a download URL, so GitHub's API limit does not apply. One shared read: the result is
/// cached, so the manifest is fetched once at startup and again only when the Update button forces it. A failed read is logged
/// and leaves the previous result in place.
/// </summary>
public static class ReleaseFeed
{
    private const string ConfigFile = "update.json";
    private const string PlaceholderRepo = "OWNER/REPO";
    private const string DefaultChannel = "latest";
    private const string DownloadUrlFormat = "https://github.com/{0}/releases/download/{1}/{2}";
    private const string UserAgent = "coop-launcher-updater";
    private const int MetadataTimeoutSeconds = 5;
    private const int DownloadChunkBytes = 81920;

    private static readonly HttpClient Client = CreateClient();
    private static readonly JsonSerializerOptions JsonOptions = new() { PropertyNameCaseInsensitive = true };
    private static readonly object Gate = new();
    private static Release? _cached;
    private static bool _read;

    private sealed record UpdateConfig(string? Repo, string? Channel);

    /// <summary>The newest release of the channel: its manifest and where its assets are.</summary>
    public sealed record Release(ReleaseManifest Manifest, IAssetSource Assets)
    {
        /// <summary>The launcher version this release carries: what <see cref="BuildCheck.LocalBuild"/> reads on a package at it.</summary>
        public int Build => Manifest.Component(InstalledRelease.LauncherId)?.Version ?? BuildInfo.DevBuild;

        /// <summary>The components of this release (the launcher, a game's mod) that are not at its version in the installed package.</summary>
        public IReadOnlyList<ManifestComponent> Changed => InstalledRelease.ReadLocal().Changed(Manifest);

        /// <summary>Some component of this release is not at its version in the installed package.</summary>
        public bool UpdateAvailable => Changed.Count > 0;
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
                if (ReadConfig() is { } config) _cached = Fetch(config) ?? _cached;
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

    private static Release? Fetch((string Repo, string Channel) config)
    {
        var source = new HttpAssetSource(asset => string.Format(DownloadUrlFormat, config.Repo, config.Channel, asset));
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(MetadataTimeoutSeconds));
        using var manifest = new MemoryStream();
        source.Download(ReleaseManifest.FileName, manifest, _ => { }, timeout.Token);
        return new Release(ReleaseManifest.Parse(Encoding.UTF8.GetString(manifest.ToArray())), source);
    }

    private static (string Repo, string Channel)? ReadConfig()
    {
        var path = Path.Combine(AppContext.BaseDirectory, ConfigFile);
        if (!File.Exists(path)) return null;
        var config = JsonSerializer.Deserialize<UpdateConfig>(File.ReadAllText(path), JsonOptions);
        if (string.IsNullOrWhiteSpace(config?.Repo) || config.Repo == PlaceholderRepo) return null;
        return (config.Repo, string.IsNullOrWhiteSpace(config.Channel) ? DefaultChannel : config.Channel);
    }

    private static HttpClient CreateClient()
    {
        var client = new HttpClient { Timeout = Timeout.InfiniteTimeSpan };
        client.DefaultRequestHeaders.UserAgent.ParseAdd(UserAgent);
        return client;
    }

    /// <summary>Downloads release assets over HTTP from the URL <paramref name="urlOf"/> gives for a name.</summary>
    private sealed class HttpAssetSource(Func<string, string> urlOf) : IAssetSource
    {
        public void Download(string asset, Stream destination, Action<int> onBytes, CancellationToken cancel)
        {
            using var response = Get(urlOf(asset), cancel);
            using var body = response.Content.ReadAsStream(cancel);
            var chunk = new byte[DownloadChunkBytes];
            for (var read = body.Read(chunk); read > 0; read = body.Read(chunk))
            {
                destination.Write(chunk, 0, read);
                onBytes(read);
            }
        }
    }
}
