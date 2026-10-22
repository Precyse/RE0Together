using System.Text.Json;
using System.Text.Json.Nodes;
using CoopLauncher;

namespace LauncherTests;

/// <summary>The release manifest is parsed strictly (a bad hash, a path that leaves the package or a missing part is refused) and
/// installed.json tells the build, with 0 for a dev build.</summary>
public static class ReleaseManifestTests
{
    private static readonly string Hash = new('a', 64);

    public static void Run()
    {
        ParsesAManifest();
        RefusesWhatCouldLeaveThePackageOrHasNoHash();
        InstalledReleaseNamesTheBuild();
    }

    private static JsonObject Valid() => JsonNode.Parse($$"""
        { "build": 7, "channel": "next", "commit": "abc",
          "full": { "asset": "Coop.zip", "sha256": "{{Hash}}", "size": 100 },
          "components": [
            { "id": "launcher", "asset": "launcher.zip", "sha256": "{{Hash}}", "size": 60,
              "files": [ { "path": "launcher/app/coop-launcher.exe", "sha256": "{{Hash}}", "size": 60 } ] },
            { "id": "re0", "asset": "mod-re0.zip", "sha256": "{{Hash}}", "size": 4,
              "files": [ { "path": "adapters/re0/dinput8.dll", "sha256": "{{Hash}}", "size": 4 } ] } ] }
        """)!.AsObject();

    private static bool Refused(JsonObject manifest)
    {
        try
        {
            ReleaseManifest.Parse(manifest.ToJsonString());
            return false;
        }
        catch (InvalidDataException)
        {
            return true;
        }
    }

    private static JsonObject With(Action<JsonObject> change)
    {
        var manifest = Valid();
        change(manifest);
        return manifest;
    }

    private static JsonObject FirstFile(JsonObject manifest) => manifest["components"]![0]!["files"]![0]!.AsObject();

    private static void ParsesAManifest()
    {
        var manifest = ReleaseManifest.Parse(Valid().ToJsonString());
        Check.That(manifest.Build == 7 && manifest.Channel == "next" && manifest.Commit == "abc", "the build, channel and commit are read");
        Check.That(manifest.Full.Asset == "Coop.zip" && manifest.Components.Count == 2, "the full package and the components are read");
        Check.That(manifest.Component("re0") is { Asset: "mod-re0.zip", Files: [{ Path: "adapters/re0/dinput8.dll", Size: 4 }] }, "a component carries its files");
        Check.That(manifest.Component("sr2") == null, "an absent component is null");
    }

    private static void RefusesWhatCouldLeaveThePackageOrHasNoHash()
    {
        Check.That(!Refused(Valid()), "the baseline manifest is valid");
        Check.That(Refused(With(m => FirstFile(m)["path"] = "../outside.dll")), "a path with .. is refused");
        Check.That(Refused(With(m => FirstFile(m)["path"] = "launcher/../../outside.dll")), "a path that climbs out is refused");
        Check.That(Refused(With(m => FirstFile(m)["path"] = "/abs/file.dll")), "a rooted path is refused");
        Check.That(Refused(With(m => FirstFile(m)["path"] = "C:/Windows/x.dll")), "a drive path is refused");
        Check.That(Refused(With(m => FirstFile(m)["path"] = "launcher\\app\\x.exe")), "a backslash path is refused");
        Check.That(Refused(With(m => FirstFile(m)["sha256"] = "XYZ")), "a bad file hash is refused");
        Check.That(Refused(With(m => FirstFile(m)["sha256"] = new string('A', 64))), "an uppercase hash is refused, the manifest is lowercase");
        Check.That(Refused(With(m => m["components"]![0]!["asset"] = "../launcher.zip")), "an asset name with a separator is refused");
        Check.That(Refused(With(m => m["components"]![0]!["sha256"] = null)), "a component without a hash is refused");
        Check.That(Refused(With(m => m["components"]![1]!["id"] = "launcher")), "a repeated component id is refused");
        Check.That(Refused(With(m => m["components"] = new JsonArray())), "no components is refused");
        Check.That(Refused(With(m => m["build"] = 0)), "build 0 is refused");
        Check.That(Refused(With(m => m.Remove("full"))), "a manifest without the full package is refused");
        Check.That(Refused(new JsonObject()), "an empty object is refused");
        try
        {
            ReleaseManifest.Parse("not json");
            Check.That(false, "text that is not json is refused");
        }
        catch (InvalidDataException)
        {
        }
    }

    private static void InstalledReleaseNamesTheBuild()
    {
        var dir = Path.Combine(Path.GetTempPath(), "cf_installed_" + Guid.NewGuid().ToString("N"));
        try
        {
            var launcherDir = Path.Combine(dir, "launcher");
            Directory.CreateDirectory(Path.Combine(launcherDir, "app"));
            var path = Path.Combine(launcherDir, InstalledRelease.FileName);
            Check.That(InstalledRelease.PathFor(Path.Combine(launcherDir, "app") + Path.DirectorySeparatorChar) == path, "in the packaged layout the file sits above the app folder");
            Check.That(InstalledRelease.PathFor(dir) == Path.Combine(dir, InstalledRelease.FileName), "elsewhere it sits beside the exe");
            Check.That(InstalledRelease.ReadBuild(path) == BuildInfo.DevBuild, "no file: a dev build");
            new InstalledRelease(12, "next", "abc").Write(path);
            Check.That(InstalledRelease.ReadBuild(path) == 12 && !File.Exists(path + ".tmp"), "the written build is read back and no temp file stays");
            File.WriteAllText(path, "{ broken");
            Check.That(InstalledRelease.ReadBuild(path) == BuildInfo.DevBuild, "an unreadable file: a dev build");
            File.WriteAllText(path, JsonSerializer.Serialize(new { build = -3 }));
            Check.That(InstalledRelease.ReadBuild(path) == BuildInfo.DevBuild, "a build below 1: a dev build");
        }
        finally
        {
            Directory.Delete(dir, recursive: true);
        }
    }
}
