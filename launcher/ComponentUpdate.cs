using System.IO.Compression;
using System.Security.Cryptography;

namespace CoopLauncher;

/// <summary>What a release changes in a package on disk: the components with at least one different file, and those files. The
/// launcher itself, or a game profile it reads at start, changing means the running copy is out of date (<see cref="RestartNeeded"/>).</summary>
public sealed record UpdatePlan(IReadOnlyList<ManifestComponent> Components, IReadOnlyList<(ManifestComponent Component, ManifestFile File)> Files)
{
    private const string LauncherFolderPrefix = "launcher/";

    public bool IsEmpty => Files.Count == 0;

    public bool RestartNeeded => Files.Any(entry => entry.File.Path.StartsWith(LauncherFolderPrefix, StringComparison.Ordinal));
}

/// <summary>
/// The part of an update that touches no installed file: working out what differs (by hashing the files on disk against the
/// manifest, so a half-updated package heals) and fetching it. Only the components with a changed file are downloaded; each zip
/// is checked against its hash and size, and every changed file inside against its own hash, before anything is staged. Any
/// mismatch throws, and nothing has been replaced.
/// </summary>
public static class ComponentUpdate
{
    private const string ZipsDirName = "zips";
    private const string ExtractedDirName = "extracted";
    private const string StagedDirName = "staged";

    public static UpdatePlan Plan(ReleaseManifest manifest, string root)
    {
        var files = manifest.Components
            .SelectMany(component => component.Files.Where(file => !Matches(file, Path.Combine(root, file.Path))).Select(file => (component, file)))
            .ToList();
        return new UpdatePlan(files.Select(entry => entry.component).Distinct().ToList(), files);
    }

    /// <summary>Downloads and checks the plan's components and returns a folder holding exactly the changed files, ready for
    /// <see cref="Updater.Replace"/>. Download progress goes to <paramref name="report"/>.</summary>
    public static string Stage(UpdatePlan plan, IAssetSource source, string workDir, ProgressReporter report, CancellationToken cancel)
    {
        var zips = Path.Combine(workDir, ZipsDirName);
        Directory.CreateDirectory(zips);
        report.Begin(ModStep.Downloading, plan.Components.Sum(component => component.Size), plan.Components.Count);
        foreach (var component in plan.Components) Fetch(component, source, Path.Combine(zips, component.Asset), report, cancel);

        var staged = Path.Combine(workDir, StagedDirName);
        foreach (var component in plan.Components)
        {
            var extracted = Path.Combine(workDir, ExtractedDirName, component.Id);
            ZipFile.ExtractToDirectory(Path.Combine(zips, component.Asset), extracted);
            foreach (var (owner, file) in plan.Files.Where(entry => entry.Component == component))
                StageFile(file, Path.Combine(extracted, file.Path), Path.Combine(staged, file.Path), owner.Id);
        }
        return staged;
    }

    public static string Sha256Of(string path)
    {
        using var stream = File.OpenRead(path);
        return Convert.ToHexString(SHA256.HashData(stream)).ToLowerInvariant();
    }

    /// <summary>Whether the file at <paramref name="path"/> is the manifest's file: same size and same hash.</summary>
    public static bool Matches(ManifestFile file, string path) =>
        File.Exists(path) && new FileInfo(path).Length == file.Size && Sha256Of(path) == file.Sha256;

    private static void Fetch(ManifestComponent component, IAssetSource source, string destination, ProgressReporter report, CancellationToken cancel)
    {
        using (var file = File.Create(destination)) source.Download(component.Asset, file, bytes => report.AddBytes(bytes), cancel);
        if (new FileInfo(destination).Length != component.Size || Sha256Of(destination) != component.Sha256)
            throw new InvalidDataException($"{component.Asset} does not match the manifest");
        report.Advance(0);
    }

    private static void StageFile(ManifestFile file, string extracted, string staged, string componentId)
    {
        if (!Matches(file, extracted)) throw new InvalidDataException($"{file.Path} in {componentId} does not match the manifest");
        Directory.CreateDirectory(Path.GetDirectoryName(staged)!);
        File.Move(extracted, staged);
    }
}
