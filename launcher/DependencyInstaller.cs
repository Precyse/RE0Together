using System.IO.Compression;
using System.Security.Cryptography;

namespace CoopLauncher;

/// <summary>
/// Installs a game's missing dependencies (a profile's <c>requires</c> entries with an <c>install</c> source, such as REFramework for
/// Dragon's Dogma 2) before its mod: each zip is downloaded, refused unless its SHA-256 is the pinned one, and only the listed entries are
/// written into the game folder (through a temporary file, never over an existing one). A dependency without a source is the player's
/// to install; the run stops with its name. Nothing is removed on uninstall: other mods use the same frameworks.
/// </summary>
public static class DependencyInstaller
{
    private const string HttpsScheme = "https";
    private const int Sha256HexLength = 64;
    private const string PartSuffix = ".part";
    private const int ChunkBytes = 81920;

    /// <summary>A download body, its length in bytes (0 when unknown) and what to dispose with it (the HTTP response).</summary>
    public readonly record struct Body(Stream Stream, long Length, IDisposable? Owner = null);

    /// <summary>Why a source cannot be used (not https, not a SHA-256, no files, a destination outside the game folder), or null.</summary>
    public static string? Problem(DependencySource source)
    {
        if (!Uri.TryCreate(source.Url, UriKind.Absolute, out var url) || url.Scheme != HttpsScheme) return "the download is not an https URL";
        if (source.Sha256.Length != Sha256HexLength || !source.Sha256.All(Uri.IsHexDigit)) return "the SHA-256 is not 64 hex digits";
        if (source.Files is not { Count: > 0 }) return "no files to take from the download";
        return source.Files.Any(file => string.IsNullOrWhiteSpace(file.From) || Inside(string.Empty, file.To) == null)
            ? "a file goes outside the game folder" : null;
    }

    /// <summary>Installs every missing dependency of the game; true when none is missing afterwards. A failure is reported with its
    /// reason (Failed) and returns false. <paramref name="open"/> replaces the HTTP download (tests).</summary>
    public static bool Install(GameProfile profile, string gameDir, IProgress<ModProgress>? progress, Func<string, Body>? open = null)
    {
        var report = new ProgressReporter(progress);
        var missing = profile.MissingRequirements(gameDir);
        if (missing.FirstOrDefault(required => required.Install == null) is { } manual)
        {
            report.Fail($"{manual.Name} must be installed by hand");
            return false;
        }
        foreach (var required in missing)
        {
            try
            {
                var zip = Download(required.Install!, open ?? Open, report);
                if (!Convert.ToHexString(SHA256.HashData(zip)).Equals(required.Install!.Sha256, StringComparison.OrdinalIgnoreCase))
                {
                    report.Fail($"{required.Name} download does not match its SHA-256");
                    return false;
                }
                Extract(zip, required.Install!, gameDir, report);
                Log.Info($"Installed {required.Name} into {gameDir}");
            }
            catch (Exception e) when (e is IOException or HttpRequestException or InvalidDataException or UnauthorizedAccessException)
            {
                report.Fail($"{required.Name}: {e.Message}");
                return false;
            }
        }
        return true;
    }

    /// <summary>The full path of <paramref name="relative"/> under <paramref name="root"/>, or null when it is rooted or climbs out.</summary>
    internal static string? Inside(string root, string relative)
    {
        if (string.IsNullOrWhiteSpace(relative) || Path.IsPathRooted(relative)) return null;
        var baseDir = Path.TrimEndingDirectorySeparator(Path.GetFullPath(root.Length == 0 ? "." : root));
        var full = Path.GetFullPath(Path.Combine(baseDir, relative));
        return full.StartsWith(baseDir + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase) ? full : null;
    }

    private static byte[] Download(DependencySource source, Func<string, Body> open, ProgressReporter report)
    {
        var body = open(source.Url);
        using var owner = body.Owner;
        using var stream = body.Stream;
        using var buffer = new MemoryStream();
        report.Begin(ModStep.Downloading, body.Length, 1);
        var chunk = new byte[ChunkBytes];
        for (var read = stream.Read(chunk); read > 0; read = stream.Read(chunk))
        {
            buffer.Write(chunk, 0, read);
            report.AddBytes(read);
        }
        return buffer.ToArray();
    }

    private static void Extract(byte[] zip, DependencySource source, string gameDir, ProgressReporter report)
    {
        using var archive = new ZipArchive(new MemoryStream(zip), ZipArchiveMode.Read);
        var entries = source.Files.Select(file => (File: file, Entry: archive.GetEntry(file.From)
            ?? throw new InvalidDataException($"the download has no {file.From}"))).ToList();
        report.Begin(ModStep.Installing, entries.Sum(pair => pair.Entry.Length), entries.Count);
        foreach (var (file, entry) in entries)
        {
            var destination = Inside(gameDir, file.To) ?? throw new InvalidDataException($"{file.To} is outside the game folder");
            if (File.Exists(destination)) continue;  // the player's own copy stays
            Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
            var part = destination + PartSuffix;
            entry.ExtractToFile(part, overwrite: true);
            File.Move(part, destination);
            report.Advance(entry.Length);
        }
    }

    private static Body Open(string url)
    {
        var response = ReleaseFeed.Get(url, CancellationToken.None);
        return new Body(response.Content.ReadAsStream(), response.Content.Headers.ContentLength ?? 0, response);
    }
}
