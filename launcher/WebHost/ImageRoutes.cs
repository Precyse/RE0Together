using CoopLauncher.Gui;
using System.Globalization;
using System.Text.RegularExpressions;

namespace CoopLauncher.Web;

/// <summary>The two private hosts the window's pages are served from (neither ever resolves in DNS).</summary>
internal static class WebHosts
{
    public const string AppOrigin = "https://app.coop.example/";
    public const string ImageOrigin = "https://img.coop.example/";
}

/// <summary>What an image URL on the image host asks for. Steam art is by app id; an avatar by Steam id (the revision is only
/// a cache key); a figure is an outfit picture (Value is an outfit option value) or, for the silhouette, a character's.</summary>
internal abstract record ImageRoute
{
    internal sealed record Steam(int AppId, SteamArt.Kind Kind) : ImageRoute;

    internal sealed record Avatar(ulong SteamId) : ImageRoute;

    internal sealed record Figure(string GameId, string Segment, string Value) : ImageRoute
    {
        public bool IsSilhouette => Value == ImageRoutes.SilhouetteWord;
    }
}

/// <summary>The image URLs the snapshot hands the page and the strict patterns that read them back. Nothing from a URL is ever a
/// file path: a route only names an id that the host then looks up among what it holds.</summary>
internal static partial class ImageRoutes
{
    public const string SilhouetteWord = "silhouette";

    private const string SteamPrefix = "steam/";
    private const string AvatarPrefix = "avatar/";
    private const string FigurePrefix = "figure/";

    [GeneratedRegex(@"^steam/(?<app>[0-9]{1,10})/(?<kind>capsule|hero|logo)$", RegexOptions.CultureInvariant)]
    private static partial Regex SteamPattern();

    [GeneratedRegex(@"^avatar/(?<id>[0-9]{1,20})$", RegexOptions.CultureInvariant)]
    private static partial Regex AvatarPattern();

    [GeneratedRegex(@"^figure/(?<game>[A-Za-z0-9_-]{1,32})/(?<segment>[A-Za-z0-9_]{1,48})/(?<value>[A-Za-z0-9_.-]{1,64})$", RegexOptions.CultureInvariant)]
    private static partial Regex FigurePattern();

    public static string SteamUrl(int appId, SteamArt.Kind kind) =>
        string.Create(CultureInfo.InvariantCulture, $"{WebHosts.ImageOrigin}{SteamPrefix}{appId}/{kind.ToString().ToLowerInvariant()}");

    public static string AvatarUrl(ulong steamId, int revision) =>
        string.Create(CultureInfo.InvariantCulture, $"{WebHosts.ImageOrigin}{AvatarPrefix}{steamId}?r={revision}");

    /// <summary>The route of a card's or stage half's figure: the outfit picture, or the character's silhouette when the outfit
    /// is not known.</summary>
    public static string FigureUrl(string gameId, Figure figure) => string.IsNullOrEmpty(figure.Value)
        ? $"{WebHosts.ImageOrigin}{FigurePrefix}{gameId}/{figure.Character}/{SilhouetteWord}"
        : $"{WebHosts.ImageOrigin}{FigurePrefix}{gameId}/{figure.OptionKey}/{figure.Value}";

    /// <summary>The route a URL on the image host names, or null for anything that is not exactly one of the three shapes.</summary>
    public static ImageRoute? Parse(string url)
    {
        if (!url.StartsWith(WebHosts.ImageOrigin, StringComparison.Ordinal)) return null;
        var path = url[WebHosts.ImageOrigin.Length..];
        var query = path.IndexOf('?');
        if (query >= 0) path = path[..query];
        if (SteamPattern().Match(path) is { Success: true } steam && int.TryParse(steam.Groups["app"].Value, NumberStyles.None, CultureInfo.InvariantCulture, out var app))
            return new ImageRoute.Steam(app, Enum.Parse<SteamArt.Kind>(steam.Groups["kind"].Value, ignoreCase: true));
        if (AvatarPattern().Match(path) is { Success: true } avatar && ulong.TryParse(avatar.Groups["id"].Value, NumberStyles.None, CultureInfo.InvariantCulture, out var id))
            return new ImageRoute.Avatar(id);
        return FigurePattern().Match(path) is { Success: true } figure
            ? new ImageRoute.Figure(figure.Groups["game"].Value, figure.Groups["segment"].Value, figure.Groups["value"].Value)
            : null;
    }
}
