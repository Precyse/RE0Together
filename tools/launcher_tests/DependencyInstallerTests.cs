using System.IO.Compression;
using System.Security.Cryptography;
using CoopLauncher;

namespace LauncherTests;

/// <summary>DependencyInstaller: a pinned zip's listed entries land in the game folder; a wrong SHA-256, a missing entry, a dependency
/// without a source and a destination outside the folder are refused; the player's own copy is never replaced.</summary>
public static class DependencyInstallerTests
{
    private const string Url = "https://example.com/owner/fw.zip";

    public static void Run()
    {
        var zip = Zip(("dinput8.dll", "framework"), ("readme.txt", "text"));
        var sha = Convert.ToHexString(SHA256.HashData(zip));
        var good = new DependencySource(Url, sha, [new DependencyFile("dinput8.dll", "dinput8.dll")]);

        Check.That(DependencyInstaller.Problem(good) == null, "a pinned https source is usable");
        Check.That(DependencyInstaller.Problem(good with { Url = "http://example.com/fw.zip" }) != null, "a plain http source is refused");
        Check.That(DependencyInstaller.Problem(good with { Sha256 = "abc" }) != null, "a short SHA-256 is refused");
        Check.That(DependencyInstaller.Problem(good with { Files = [new DependencyFile("dinput8.dll", "../dinput8.dll")] }) != null,
            "a file outside the game folder is refused");

        WithFolder(dir =>
        {
            var game = Profile(new RequiredFile("dinput8.dll", "Framework", good));
            Check.That(game.MissingRequirement(dir) == "Framework", "the missing dependency is named");
            Check.That(DependencyInstaller.Install(game, dir, null, Serve(zip)) && File.ReadAllText(Path.Combine(dir, "dinput8.dll")) == "framework",
                "the listed entry is installed");
            Check.That(!File.Exists(Path.Combine(dir, "readme.txt")) && game.MissingRequirement(dir) == null, "only the listed entries are taken");
        });
        WithFolder(dir =>
        {
            var game = Profile(new RequiredFile("dinput8.dll", "Framework", good with { Sha256 = new string('0', 64) }));
            Check.That(!DependencyInstaller.Install(game, dir, null, Serve(zip)) && !File.Exists(Path.Combine(dir, "dinput8.dll")),
                "a download that does not match its SHA-256 installs nothing");
        });
        WithFolder(dir =>
        {
            var game = Profile(new RequiredFile("dinput8.dll", "Framework", good with { Files = [new DependencyFile("absent.dll", "dinput8.dll")] }));
            Check.That(!DependencyInstaller.Install(game, dir, null, Serve(zip)), "a download without the listed entry fails");
        });
        WithFolder(dir =>
        {
            var game = Profile(new RequiredFile("dinput8.dll", "Framework"));
            var reports = new List<ModProgress>();
            Check.That(!DependencyInstaller.Install(game, dir, new ListProgress(reports), Serve(zip)) &&
                       reports.LastOrDefault().Reason == "Framework must be installed by hand",
                "a dependency without a source stops the install with its name");
        });
        WithFolder(dir =>
        {
            File.WriteAllText(Path.Combine(dir, "other.dll"), "player's");
            var game = Profile(new RequiredFile("dinput8.dll", "Framework", good with { Files = [new DependencyFile("dinput8.dll", "dinput8.dll"), new DependencyFile("readme.txt", "other.dll")] }));
            Check.That(DependencyInstaller.Install(game, dir, null, Serve(zip)) && File.ReadAllText(Path.Combine(dir, "other.dll")) == "player's",
                "a file the player already has is left alone");
        });
    }

    private static GameProfile Profile(RequiredFile required) =>
        new("test", "Test", 1, "test.exe", 2, 1, [], "coop/adapter.ini", Requires: [required]);

    private static Func<string, DependencyInstaller.Body> Serve(byte[] zip) => url =>
        url == Url ? new DependencyInstaller.Body(new MemoryStream(zip), zip.Length) : throw new HttpRequestException("unknown URL");

    private static byte[] Zip(params (string Name, string Text)[] entries)
    {
        using var buffer = new MemoryStream();
        using (var archive = new ZipArchive(buffer, ZipArchiveMode.Create, leaveOpen: true))
        {
            foreach (var (name, text) in entries)
            {
                using var writer = new StreamWriter(archive.CreateEntry(name).Open());
                writer.Write(text);
            }
        }
        return buffer.ToArray();
    }

    private static void WithFolder(Action<string> body)
    {
        var dir = Path.Combine(Path.GetTempPath(), "cf_deps_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(dir);
        try
        {
            body(dir);
        }
        finally
        {
            Directory.Delete(dir, recursive: true);
        }
    }

    private sealed class ListProgress(List<ModProgress> reports) : IProgress<ModProgress>
    {
        public void Report(ModProgress value) => reports.Add(value);
    }
}
