using CoopLauncher.Gui;
using Microsoft.Web.WebView2.Core;
using System.Collections.Concurrent;
using System.Drawing.Imaging;
using System.Reflection;

namespace CoopLauncher.Web;

/// <summary>Answers every request the pages make, for both private hosts, through <c>WebResourceRequested</c>: the page files from
/// the manifest resources of the exe (SPEC 7: no folder next to the exe) and the images (SPEC 3.4) loaded in the background behind
/// a deferral. Routes are matched by <see cref="ImageRoutes"/>'s fixed patterns and only name ids the launcher holds; nothing from
/// a URL reaches a file path. Anything unknown is a 404.</summary>
internal sealed class WebAssets
{
    public const int FigureCacheCapacity = 64;

    private const string ResourcePrefix = "web/";
    private const string IndexPage = "index.html";
    private const string NoStore = "no-store";
    private const string RevisionedImage = "private, max-age=31536000, immutable";
    private const string NoCache = "no-cache";
    private const string PngType = "image/png";
    private const int NotFound = 404;
    private const int Ok = 200;

    private static readonly Dictionary<string, string> MimeTypes = new(StringComparer.OrdinalIgnoreCase)
    {
        [".html"] = "text/html; charset=utf-8",
        [".css"] = "text/css; charset=utf-8",
        [".js"] = "text/javascript; charset=utf-8",
        [".svg"] = "image/svg+xml",
        [".png"] = PngType,
        [".jpg"] = "image/jpeg",
        [".jpeg"] = "image/jpeg",
        [".ico"] = "image/x-icon",
    };

    private readonly IReadOnlyList<GameProfile> _profiles;
    private readonly AvatarImages _avatars;
    private readonly Assembly _assembly;
    private readonly Dictionary<string, string> _resources;
    private readonly Lru<string, byte[]> _figures = new(FigureCacheCapacity);
    private readonly ConcurrentDictionary<string, bool> _failed = new();
    private readonly TaskScheduler _ui = TaskScheduler.FromCurrentSynchronizationContext();

    /// <summary>Created on the UI thread: responses are built on it.</summary>
    public WebAssets(IReadOnlyList<GameProfile> profiles, AvatarImages avatars, Assembly? resources = null)
    {
        _profiles = profiles;
        _avatars = avatars;
        _assembly = resources ?? typeof(WebAssets).Assembly;
        _resources = _assembly.GetManifestResourceNames()
            .Where(name => name.Replace('\\', '/').StartsWith(ResourcePrefix, StringComparison.Ordinal))
            .ToDictionary(name => name.Replace('\\', '/')[ResourcePrefix.Length..], name => name, StringComparer.Ordinal);
    }

    /// <summary>Takes over the requests of both hosts for a web view.</summary>
    public void Attach(CoreWebView2 core)
    {
        core.AddWebResourceRequestedFilter(WebHosts.AppOrigin + "*", CoreWebView2WebResourceContext.All);
        core.AddWebResourceRequestedFilter(WebHosts.ImageOrigin + "*", CoreWebView2WebResourceContext.All);
        core.WebResourceRequested += OnRequested;
    }

    /// <summary>The embedded page files, by their path under the page root (for tests of the packaging).</summary>
    public IReadOnlyCollection<string> PagePaths => _resources.Keys;

    private void OnRequested(object? sender, CoreWebView2WebResourceRequestedEventArgs e)
    {
        var core = (CoreWebView2)sender!;
        var uri = e.Request.Uri;
        if (e.Request.Method != "GET") e.Response = Respond(core, null, null, NoStore, NotFound);
        else if (uri.StartsWith(WebHosts.AppOrigin, StringComparison.Ordinal)) e.Response = ServePage(core, uri[WebHosts.AppOrigin.Length..]);
        else if (uri.StartsWith(WebHosts.ImageOrigin, StringComparison.Ordinal)) ServeImage(core, e, uri);
        else e.Response = Respond(core, null, null, NoStore, NotFound);
    }

    private CoreWebView2WebResourceResponse ServePage(CoreWebView2 core, string rawPath)
    {
        var path = StripQuery(rawPath);
        if (path.Length == 0) path = IndexPage;
        if (!MimeTypes.TryGetValue(Path.GetExtension(path), out var mime) || !_resources.TryGetValue(path, out var resource))
            return Respond(core, null, null, NoStore, NotFound);
        return Respond(core, _assembly.GetManifestResourceStream(resource), mime, NoStore, Ok);
    }

    private void ServeImage(CoreWebView2 core, CoreWebView2WebResourceRequestedEventArgs e, string uri)
    {
        var route = ImageRoutes.Parse(uri);
        if (route == null)
        {
            e.Response = Respond(core, null, null, NoStore, NotFound);
            return;
        }
        var deferral = e.GetDeferral();
        Task.Run(() => LoadImage(route)).ContinueWith(task =>
        {
            e.Response = task.Result is { } image
                ? Respond(core, new MemoryStream(image.Bytes), image.ContentType, route is ImageRoute.Avatar ? RevisionedImage : NoCache, Ok)
                : Respond(core, null, null, NoStore, NotFound);
            deferral.Complete();
        }, CancellationToken.None, TaskContinuationOptions.None, _ui);
    }

    private static CoreWebView2WebResourceResponse Respond(CoreWebView2 core, Stream? body, string? contentType, string cacheControl, int status)
    {
        var headers = $"Cache-Control: {cacheControl}\r\nX-Content-Type-Options: nosniff" + (contentType == null ? string.Empty : $"\r\nContent-Type: {contentType}");
        return core.Environment.CreateWebResourceResponse(body, status, status == Ok ? "OK" : "Not Found", headers);
    }

    private static string StripQuery(string path)
    {
        var end = path.IndexOfAny(['?', '#']);
        return end < 0 ? path : path[..end];
    }

    // ---------------------------------------------------------------- images (background thread)

    private sealed record ImageBytes(byte[] Bytes, string ContentType);

    private ImageBytes? LoadImage(ImageRoute route)
    {
        try
        {
            return route switch
            {
                ImageRoute.Steam steam => LoadSteam(steam),
                ImageRoute.Avatar avatar => _avatars.Png(avatar.SteamId) is { } png ? new ImageBytes(png, PngType) : null,
                ImageRoute.Figure figure => LoadFigure(figure),
                _ => null,
            };
        }
        catch (Exception e)
        {
            // The boundary of the background load: whatever goes wrong, the request must still be answered (as a 404).
            if (_failed.TryAdd(route.ToString()!, true)) Log.Info($"Image {route} unreadable: {e.Message}");
            return null;
        }
    }

    /// <summary>Steam's library art of a game this launcher loaded.</summary>
    private ImageBytes? LoadSteam(ImageRoute.Steam route)
    {
        if (_profiles.All(profile => profile.SteamAppId != route.AppId)) return null;
        if (SteamArt.FindFile(route.AppId, route.Kind) is not { } path) return null;
        return MimeTypes.TryGetValue(Path.GetExtension(path), out var mime) ? new ImageBytes(File.ReadAllBytes(path), mime) : null;
    }

    private ImageBytes? LoadFigure(ImageRoute.Figure route)
    {
        var key = $"{route.GameId}/{route.Segment}/{route.Value}";
        if (_figures.TryGet(key, out var cached)) return new ImageBytes(cached!, PngType);
        if (_profiles.FirstOrDefault(profile => profile.Id == route.GameId) is not { } game || GameFolders.Find(game) is not { } gameDir) return null;
        using var picture = route.IsSilhouette ? Silhouette(game, gameDir, route.Segment) : Outfit(game, gameDir, route.Segment, route.Value);
        if (picture == null) return null;
        using var stream = new MemoryStream();
        picture.Save(stream, ImageFormat.Png);
        var bytes = stream.ToArray();
        _figures.Put(key, bytes);
        return new ImageBytes(bytes, PngType);
    }

    /// <summary>An outfit's picture: the option must be an outfit option of the game and the value one it offers in this folder.</summary>
    private Bitmap? Outfit(GameProfile game, string gameDir, string optionKey, string value)
    {
        var option = game.Options?.FirstOrDefault(candidate => candidate.Character != null && candidate.Key == optionKey);
        var offered = option?.AvailableIn(gameDir).FirstOrDefault(candidate => candidate.Value == value);
        return offered == null ? null : ReadOnce(game, gameDir, offered);
    }

    /// <summary>A character's shape in one gray: from the first of its outfits that has a picture.</summary>
    private Bitmap? Silhouette(GameProfile game, string gameDir, string character)
    {
        if (GameOptions.OutfitOf(game, character) is not { } option) return null;
        foreach (var value in option.AvailableIn(gameDir))
        {
            using var picture = ReadOnce(game, gameDir, value);
            if (picture != null) return OutfitArt.Silhouette(picture, Theme.Raised);
        }
        return null;
    }

    /// <summary>The picture of an outfit; one that cannot be read is logged once and has none.</summary>
    private Bitmap? ReadOnce(GameProfile game, string gameDir, OptionValue value)
    {
        try
        {
            return OutfitArt.Load(game, gameDir, value);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or InvalidDataException or ArgumentException)
        {
            if (_failed.TryAdd($"{game.Id}/{value.Value}", true)) Log.Info($"Outfit art {value.Value} unreadable: {e.Message}");
            return null;
        }
    }
}
