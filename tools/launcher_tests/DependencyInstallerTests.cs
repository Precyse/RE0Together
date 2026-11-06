using System.IO.Compression;
using System.Security.Cryptography;
using CoopLauncher;

namespace LauncherTests;

/// <summary>DependencyInstaller: a pinned zip's listed entries land in the game folder; a wrong SHA-256, a missing entry, a dependency
/// without a source and a destination outside the folder are refused; the player's own copy is never replaced, except a required file that
/// is another version than the accepted ones, which is archived first; a failed URL falls through to the next, and a failed download says
/// so in plain words.</summary>
public static class DependencyInstallerTests
{
    private const string Url = "https://example.com/owner/fw.zip";
    private const string MirrorUrl = "https://mirror.example.com/fw.zip";
    private const string DownloadFailed = "Could not download Framework: check your connection, then press Install again";
    private const string ProfileHead =
        "{ \"id\": \"t\", \"name\": \"T\", \"steamAppId\": 1, \"exe\": \"t.exe\", \"maxPlayers\": 2, \"port\": 1, \"adapterFiles\": [], \"adapterIni\": \"a.ini\", ";

    public static void Run()
    {
        var zip = Zip(("dinput8.dll", "framework"), ("readme.txt", "text"));
        var sha = Convert.ToHexString(SHA256.HashData(zip));
        var good = new DependencySource([Url], sha, [new DependencyFile("dinput8.dll", "dinput8.dll")]);

        Check.That(DependencyInstaller.Problem(good) == null, "a pinned https source is usable");
        Check.That(DependencyInstaller.Problem(good with { Urls = ["http://example.com/fw.zip"] }) != null, "a plain http source is refused");
        Check.That(DependencyInstaller.Problem(good with { Urls = [Url, "http://example.com/fw.zip"] }) != null, "a plain http fallback is refused");
        Check.That(DependencyInstaller.Problem(good with { Urls = [] }) != null, "a source without a URL is refused");
        Check.That(DependencyInstaller.Problem(good with { Sha256 = "abc" }) != null, "a short SHA-256 is refused");
        Check.That(DependencyInstaller.Problem(good with { Files = [new DependencyFile("dinput8.dll", "../dinput8.dll")] }) != null,
            "a file outside the game folder is refused");

        WithFolder(dir =>
        {
            var game = Profile(new RequiredFile("dinput8.dll", "Framework", good));
            Check.That(game.UnmetRequirement(dir) is { Name: "Framework", OtherVersion: false }, "the missing dependency is named");
            Check.That(DependencyInstaller.Install(game, dir, null, Serve(zip)) && File.ReadAllText(Path.Combine(dir, "dinput8.dll")) == "framework",
                "the listed entry is installed");
            Check.That(!File.Exists(Path.Combine(dir, "readme.txt")) && game.UnmetRequirement(dir) == null, "only the listed entries are taken");
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
        Versions(zip, good);
        Downloads(zip, good);
        Profiles();
    }

    private static void Versions(byte[] zip, DependencySource good)
    {
        var accepted = Convert.ToHexString(SHA256.HashData("framework"u8.ToArray())).ToLowerInvariant();
        var picky = new RequiredFile("dinput8.dll", "Framework", good, [accepted]);
        WithFolder(dir =>
        {
            File.WriteAllText(Path.Combine(dir, "dinput8.dll"), "framework");
            Check.That(Profile(picky).UnmetRequirement(dir) == null, "an accepted build satisfies the requirement");
            File.WriteAllText(Path.Combine(dir, "dinput8.dll"), "nexus build");
            Check.That(Profile(picky).UnmetRequirement(dir) is { Name: "Framework", OtherVersion: true }, "another build is another version");
            Check.That(Profile(picky with { Accept = null }).UnmetRequirement(dir) == null, "without accept any file of that name will do");
        });
        WithFolder(dir =>
        {
            File.WriteAllText(Path.Combine(dir, "dinput8.dll"), "nexus build");
            Check.That(DependencyInstaller.Install(Profile(picky), dir, null, Serve(zip)) && File.ReadAllText(Path.Combine(dir, "dinput8.dll")) == "framework",
                "replacing installs the pinned build");
            var archived = Directory.GetFiles(Path.Combine(dir, "coop", "archive"), "dinput8.dll", SearchOption.AllDirectories);
            Check.That(archived is [var old] && File.ReadAllText(old) == "nexus build" && Path.GetFileName(Path.GetDirectoryName(old))!.StartsWith("framework_"),
                "the old build is moved to coop/archive/<name>_<stamp>, not deleted");
        });
        WithFolder(dir =>
        {
            File.WriteAllText(Path.Combine(dir, "dinput8.dll"), "nexus build");
            var offline = new DependencyInstaller.Opener((_, _) => throw new HttpRequestException("offline"));
            Check.That(!DependencyInstaller.Install(Profile(picky), dir, null, offline) && File.ReadAllText(Path.Combine(dir, "dinput8.dll")) == "nexus build" &&
                       !Directory.Exists(Path.Combine(dir, "coop")), "a failed download leaves the old build in place");
        });
    }

    private static void Downloads(byte[] zip, DependencySource good)
    {
        var mirrored = Profile(new RequiredFile("dinput8.dll", "Framework", good with { Urls = [Url, MirrorUrl] }));
        WithFolder(dir =>
        {
            var fallback = new DependencyInstaller.Opener((url, _) => url == MirrorUrl
                ? new DependencyInstaller.Body(new MemoryStream(zip), zip.Length) : throw new HttpRequestException("gone"));
            Check.That(DependencyInstaller.Install(mirrored, dir, null, fallback) && File.Exists(Path.Combine(dir, "dinput8.dll")),
                "the next URL is tried when the first fails");
        });
        foreach (Exception failure in new Exception[] { new HttpRequestException("offline"), new OperationCanceledException(), new IOException("reset") })
        {
            WithFolder(dir =>
            {
                var reports = new List<ModProgress>();
                var broken = new DependencyInstaller.Opener((_, _) => throw failure);
                Check.That(!DependencyInstaller.Install(mirrored, dir, new ListProgress(reports), broken) && reports.LastOrDefault().Reason == DownloadFailed,
                    $"every URL failing ({failure.GetType().Name}) says to check the connection and press Install again");
            });
        }
        using var cancelled = new CancellationTokenSource();
        cancelled.Cancel();
        var threw = false;
        try
        {
            ReleaseFeed.ReadChunk(new NeverEndingStream(), new byte[8], cancelled.Token);
        }
        catch (OperationCanceledException)
        {
            threw = true;
        }
        Check.That(threw, "a read that gets no bytes ends when its token fires");
    }

    private static void Profiles()
    {
        var sha = new string('a', 64);
        var requires = $"\"requires\": [ {{ \"file\": \"x.dll\", \"name\": \"X\", \"accept\": [ \"{sha}\" ], " +
                       $"\"install\": {{ \"urls\": [ \"{Url}\" ], \"sha256\": \"{sha}\", \"files\": [ {{ \"from\": \"x.dll\", \"to\": \"x.dll\" }} ] }} }} ] }}";
        var parsed = GameProfile.Parse("t", ProfileHead + requires, ".");
        Check.That(parsed.Requires![0].Accept is [var accepted] && accepted == sha && parsed.Requires[0].Install!.Urls is [Url],
            "accept and urls are read from the profile");
        var threw = false;
        try
        {
            GameProfile.Parse("t", ProfileHead + "\"requires\": [ { \"file\": \"x.dll\", \"name\": \"X\", \"accept\": [ \"abc\" ] } ] }", ".");
        }
        catch (InvalidDataException)
        {
            threw = true;
        }
        Check.That(threw, "an accepted hash that is not a SHA-256 is refused at load");
    }

    private static GameProfile Profile(RequiredFile required) =>
        new("test", "Test", 1, "test.exe", 2, 1, [], "coop/adapter.ini", Requires: [required]);

    private static DependencyInstaller.Opener Serve(byte[] zip) => (url, _) =>
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

    private sealed class NeverEndingStream : MemoryStream
    {
        public override async ValueTask<int> ReadAsync(Memory<byte> buffer, CancellationToken cancel = default)
        {
            await Task.Delay(Timeout.Infinite, cancel);
            return 0;
        }
    }

    private sealed class ListProgress(List<ModProgress> reports) : IProgress<ModProgress>
    {
        public void Report(ModProgress value) => reports.Add(value);
    }
}
