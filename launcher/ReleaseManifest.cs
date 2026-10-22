using System.Text.Json;
using System.Text.RegularExpressions;

namespace CoopLauncher;

/// <summary>One file of a component: its path below the package root (forward slashes), SHA-256 (lowercase hex) and size.</summary>
public sealed record ManifestFile(string Path, string Sha256, long Size);

/// <summary>A release asset (a zip) with the SHA-256 and size it must have.</summary>
public sealed record ManifestAsset(string Asset, string Sha256, long Size);

/// <summary>A part of the package that updates on its own: the launcher, or one game's mod (its profile and adapter files). Its
/// zip is <see cref="Asset"/>; <see cref="Files"/> are the files the zip carries, each with its hash.</summary>
public sealed record ManifestComponent(string Id, string Asset, string Sha256, long Size, List<ManifestFile> Files);

/// <summary>
/// The release's manifest.json, published beside the zips: the build number, the channel, the commit, the full package
/// (<see cref="Full"/>, for first installs) and every component with its files' hashes. Parsing refuses anything that could
/// write outside the package root or that has no usable hash.
/// </summary>
public sealed record ReleaseManifest(int Build, string Channel, string Commit, ManifestAsset Full, List<ManifestComponent> Components)
{
    public const string FileName = "manifest.json";

    private static readonly JsonSerializerOptions JsonOptions = new() { PropertyNameCaseInsensitive = true };
    private static readonly Regex Hex256 = new("^[0-9a-f]{64}$", RegexOptions.Compiled);

    /// <summary>The manifest in <paramref name="json"/>; <see cref="InvalidDataException"/> when it is not one.</summary>
    public static ReleaseManifest Parse(string json)
    {
        ReleaseManifest? manifest;
        try
        {
            manifest = JsonSerializer.Deserialize<ReleaseManifest>(json, JsonOptions);
        }
        catch (JsonException e)
        {
            throw new InvalidDataException($"manifest is not valid: {e.Message}");
        }
        return manifest is { Components: not null, Full: not null } ? manifest.Validated() : throw new InvalidDataException("manifest is empty");
    }

    public ManifestComponent? Component(string id) => Components.FirstOrDefault(component => component.Id == id);

    private ReleaseManifest Validated()
    {
        Require(Build > 0, "build");
        Require(!string.IsNullOrWhiteSpace(Channel), "channel");
        CheckAsset(Full.Asset, Full.Sha256, Full.Size);
        Require(Components.Count > 0 && Components.Select(c => c.Id).Distinct().Count() == Components.Count, "components");
        foreach (var component in Components)
        {
            Require(!string.IsNullOrWhiteSpace(component.Id) && component.Files is { Count: > 0 }, $"component {component.Id}");
            CheckAsset(component.Asset, component.Sha256, component.Size);
            foreach (var file in component.Files)
            {
                Require(IsPackagePath(file.Path), $"file path {file.Path}");
                Require(Hex256.IsMatch(file.Sha256 ?? string.Empty) && file.Size >= 0, $"file {file.Path}");
            }
        }
        return this;
    }

    private static void CheckAsset(string? asset, string? sha256, long size)
    {
        Require(!string.IsNullOrWhiteSpace(asset) && asset.IndexOfAny(['/', '\\']) < 0 && asset != "..", $"asset name {asset}");
        Require(Hex256.IsMatch(sha256 ?? string.Empty) && size > 0, $"asset {asset}");
    }

    private static bool IsPackagePath(string? path) =>
        !string.IsNullOrEmpty(path) && !path.Contains('\\') && !Path.IsPathRooted(path) && !path.Contains(':')
        && path.Split('/').All(part => part.Length > 0 && part != "." && part != "..");

    private static void Require(bool ok, string what)
    {
        if (!ok) throw new InvalidDataException($"manifest has a bad {what}");
    }
}
