using System.Text.Json;

namespace CoopLauncher;

/// <summary>
/// Which release a package on disk came from: <c>installed.json</c> ({build, channel, commit}), written into the package by the
/// release build and by the updater when it finishes. In the packaged layout it sits in <c>root\launcher</c>, above the app
/// folder; a launcher running from anywhere else (a dev build, a test copy) keeps it beside the exe. No file means a dev build
/// (<see cref="BuildInfo.DevBuild"/>).
/// </summary>
public sealed record InstalledRelease(int Build, string? Channel = null, string? Commit = null)
{
    public const string FileName = "installed.json";
    public const string AppDirName = "app";
    public const string LauncherDirName = "launcher";

    private const string TempSuffix = ".tmp";

    private static readonly JsonSerializerOptions JsonOptions = new() { PropertyNameCaseInsensitive = true, WriteIndented = true };

    /// <summary>Where the file of the launcher running from <paramref name="appDir"/> lives.</summary>
    public static string PathFor(string appDir)
    {
        var dir = new DirectoryInfo(appDir.TrimEnd('\\', '/'));
        return dir.Name == AppDirName && dir.Parent is { } parent ? Path.Combine(parent.FullName, FileName) : Path.Combine(dir.FullName, FileName);
    }

    /// <summary>The build of the file at <paramref name="path"/>; 0 (a dev build) when it is missing or unreadable.</summary>
    public static int ReadBuild(string path)
    {
        try
        {
            return JsonSerializer.Deserialize<InstalledRelease>(File.ReadAllText(path), JsonOptions)?.Build is > 0 and var build ? build : BuildInfo.DevBuild;
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or JsonException)
        {
            return BuildInfo.DevBuild;
        }
    }

    /// <summary>Writes the file whole beside itself and moves it into place, so a cut-short write never leaves half a file.</summary>
    public void Write(string path)
    {
        var temp = path + TempSuffix;
        File.WriteAllText(temp, JsonSerializer.Serialize(this, JsonOptions));
        File.Move(temp, path, overwrite: true);
    }
}
