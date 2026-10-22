using System.IO.Compression;
using System.Security.Cryptography;
using System.Text.Json;
using CoopLauncher;

namespace LauncherTests;

/// <summary>A release built in a temp folder, with no network: <see cref="Set"/> puts files into a package, <see cref="Publish"/>
/// zips each component and writes the manifest, and <see cref="Source"/> hands the zips to the updater like the release does.
/// A component's version counts up in the publish where its files changed, like the real release does.</summary>
internal sealed class ReleaseFixture : IDisposable
{
    public const string LauncherExe = "launcher/app/coop-launcher.exe";
    public const string Re0Profile = "launcher/games/re0.json";
    public const string Re0Dll = "adapters/re0/dinput8.dll";
    public const string Ds2Dll = "adapters/ds2/version.dll";

    private static readonly Dictionary<string, string[]> Components = new()
    {
        ["launcher"] = [LauncherExe, "launcher/app/update.json"],
        ["re0"] = [Re0Profile, Re0Dll],
        ["ds2"] = ["launcher/games/ds2.json", Ds2Dll],
    };

    private readonly Dictionary<string, int> _versions = [];
    private readonly Dictionary<string, string> _digests = [];
    private int _published;

    public ReleaseFixture()
    {
        Dir = Path.Combine(Path.GetTempPath(), "cf_release_" + Guid.NewGuid().ToString("N"));
        Package = Path.Combine(Dir, "package");
        Assets = Path.Combine(Dir, "assets");
        Root = Path.Combine(Dir, "root");
        Directory.CreateDirectory(Assets);
        foreach (var path in Components.Values.SelectMany(paths => paths)) Set(path, "v1 " + path);
    }

    public string Dir { get; }

    /// <summary>The package the release is built from.</summary>
    public string Package { get; }

    /// <summary>The asset folder: what the release holds.</summary>
    public string Assets { get; }

    /// <summary>An installed package (the folder an updater works on).</summary>
    public string Root { get; }

    public AssetFolder Source => new(Assets);

    public void Set(string path, string text)
    {
        var full = Path.Combine(Package, path);
        Directory.CreateDirectory(Path.GetDirectoryName(full)!);
        File.WriteAllText(full, text);
    }

    /// <summary>Zips the components of the package as it is now into the asset folder and returns the release's manifest
    /// (also written as manifest.json). Build numbers count up with each call, a component's version only when its files changed.</summary>
    public ReleaseManifest Publish()
    {
        _published++;
        var components = Components.Select(entry => Zip(entry.Key, entry.Value)).ToList();
        var full = ZipAsset(Path.Combine(Assets, "Coop.zip"), Components.Values.SelectMany(paths => paths));
        var manifest = new ReleaseManifest(_published, "next", "abc123", full, components);
        File.WriteAllText(Path.Combine(Assets, ReleaseManifest.FileName), JsonSerializer.Serialize(manifest));
        return ReleaseManifest.Parse(File.ReadAllText(Path.Combine(Assets, ReleaseManifest.FileName)));
    }

    /// <summary>Copies the package into the installed root as the release published last, with its installed.json.</summary>
    public void InstallPackage()
    {
        Directory.CreateDirectory(Path.Combine(Root, "launcher"));
        new InstalledRelease(_published, Components: new Dictionary<string, int>(_versions)).Write(Path.Combine(Root, "launcher", InstalledRelease.FileName));
        foreach (var path in Components.Values.SelectMany(paths => paths))
        {
            Directory.CreateDirectory(Path.GetDirectoryName(Path.Combine(Root, path))!);
            File.Copy(Path.Combine(Package, path), Path.Combine(Root, path));
        }
    }

    public string ReadRoot(string path) => File.ReadAllText(Path.Combine(Root, path));

    public InstalledRelease Installed() => InstalledRelease.Read(Path.Combine(Root, "launcher", InstalledRelease.FileName));

    /// <summary>installed.json names exactly the versions of <paramref name="manifest"/>.</summary>
    public bool IsAt(ReleaseManifest manifest) => Installed().Matches(manifest);

    public void Dispose() => Directory.Delete(Dir, recursive: true);

    private ManifestComponent Zip(string id, string[] paths)
    {
        var asset = ZipAsset(Path.Combine(Assets, id + ".zip"), paths);
        var files = paths.Select(path => new ManifestFile(path, ComponentUpdate.Sha256Of(Path.Combine(Package, path)), new FileInfo(Path.Combine(Package, path)).Length)).ToList();
        var digest = string.Join('|', files.Select(file => file.Sha256));
        if (!_digests.TryGetValue(id, out var last) || last != digest) _versions[id] = _versions.GetValueOrDefault(id) + 1;
        _digests[id] = digest;
        return new ManifestComponent(id, _versions[id], id + ".zip", asset.Sha256, asset.Size, files);
    }

    private ManifestAsset ZipAsset(string zipPath, IEnumerable<string> paths)
    {
        File.Delete(zipPath);
        using (var zip = ZipFile.Open(zipPath, ZipArchiveMode.Create))
            foreach (var path in paths) zip.CreateEntryFromFile(Path.Combine(Package, path), path);
        return new ManifestAsset(Path.GetFileName(zipPath), Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(zipPath))).ToLowerInvariant(), new FileInfo(zipPath).Length);
    }

    /// <summary>Release assets from a folder; remembers which were asked for.</summary>
    internal sealed class AssetFolder(string dir) : IAssetSource
    {
        public List<string> Requested { get; } = [];

        public void Download(string asset, Stream destination, Action<int> onBytes, CancellationToken cancel)
        {
            Requested.Add(asset);
            var bytes = File.ReadAllBytes(Path.Combine(dir, asset));
            destination.Write(bytes);
            onBytes(bytes.Length);
        }
    }
}
