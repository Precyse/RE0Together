using System.Text.Json;

namespace CoopLauncher;

/// <summary>
/// Which release a package on disk came from: <c>installed.json</c> ({build, channel, commit, components}), written into the package by
/// the release build and by the updater when it finishes. <see cref="Build"/> is the release (the CI run number); <see cref="Components"/>
/// holds the version each component of the package is at (the launcher and one mod per game, each counting up only when that
/// component changes). In the packaged layout the file sits in <c>root\launcher</c>, above the app folder; a launcher running from
/// anywhere else (a dev build, a test copy) keeps it beside the exe. No file, or none that names the component, means a dev build
/// (<see cref="BuildInfo.DevBuild"/>) for it.
/// </summary>
public sealed record InstalledRelease(int Build, string? Channel = null, string? Commit = null, Dictionary<string, int>? Components = null)
{
    public const string FileName = "installed.json";
    public const string AppDirName = "app";
    public const string LauncherDirName = "launcher";
    public const string LauncherId = "launcher";

    private const string TempSuffix = ".tmp";

    private static readonly JsonSerializerOptions JsonOptions = new() { PropertyNameCaseInsensitive = true, WriteIndented = true };

    /// <summary>Where the file of the launcher running from <paramref name="appDir"/> lives.</summary>
    public static string PathFor(string appDir)
    {
        var dir = new DirectoryInfo(appDir.TrimEnd('\\', '/'));
        return dir.Name == AppDirName && dir.Parent is { } parent ? Path.Combine(parent.FullName, FileName) : Path.Combine(dir.FullName, FileName);
    }

    /// <summary>The file of the package this launcher runs from.</summary>
    public static InstalledRelease ReadLocal() => Read(PathFor(AppContext.BaseDirectory));

    /// <summary>The file at <paramref name="path"/>; the record of a dev build (no versions) when it is missing or unreadable.</summary>
    public static InstalledRelease Read(string path)
    {
        try
        {
            return JsonSerializer.Deserialize<InstalledRelease>(File.ReadAllText(path), JsonOptions) ?? new InstalledRelease(BuildInfo.DevBuild);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or JsonException)
        {
            return new InstalledRelease(BuildInfo.DevBuild);
        }
    }

    /// <summary>The version a component is at; 0 (a dev build) when the file does not name it.</summary>
    public int VersionOf(string componentId) =>
        Components != null && Components.TryGetValue(componentId, out var version) && version > 0 ? version : BuildInfo.DevBuild;

    /// <summary>The components of the manifest whose version here differs from the manifest's.</summary>
    public IReadOnlyList<ManifestComponent> Changed(ReleaseManifest manifest) =>
        manifest.Components.Where(component => VersionOf(component.Id) != component.Version).ToList();

    /// <summary>Every component of the manifest is at the manifest's version.</summary>
    public bool Matches(ReleaseManifest manifest) => Changed(manifest).Count == 0;

    /// <summary>Writes the file whole beside itself and moves it into place, so a cut-short write never leaves half a file.</summary>
    public void Write(string path)
    {
        var temp = path + TempSuffix;
        File.WriteAllText(temp, JsonSerializer.Serialize(this, JsonOptions));
        File.Move(temp, path, overwrite: true);
    }
}
