using System.IO.Compression;
using System.Security.Cryptography;

namespace CoopLauncher;

/// <summary>
/// Installs a game's unmet dependencies (a profile's <c>requires</c> entries with an <c>install</c> source, such as REFramework for
/// Dragon's Dogma 2) before its mod: each zip is downloaded (from the first URL that works), refused unless its SHA-256 is the pinned
/// one, and only the listed entries are written into the game folder (through a temporary file, never over an existing one). A file that
/// is there but another version than the accepted ones is first moved, never deleted, to <c>coop/archive/&lt;name&gt;_&lt;stamp&gt;/</c>
/// (the Replace of the dependencies popup). A dependency without a source is the player's to install; the run stops with its name. A
/// download that stalls or runs too long fails the run in plain words. Nothing is removed on uninstall: other mods use the same frameworks.
/// </summary>
public static class DependencyInstaller
{
    private const string HttpsScheme = "https";
    private const int Sha256HexLength = 64;
    private const string PartSuffix = ".part";
    private const int ChunkBytes = 81920;
    private const string ArchiveFolder = "coop/archive";
    private const string DownloadFailedFormat = "Could not download {0}: check your connection, then press Install again";

    /// <summary>A download body, its length in bytes (0 when unknown) and what to dispose with it (the HTTP response).</summary>
    public readonly record struct Body(Stream Stream, long Length, IDisposable? Owner = null);

    /// <summary>Opens a download: the URL and the token that ends the wait for the server's answer.</summary>
    public delegate Body Opener(string url, CancellationToken cancel);

    /// <summary>Why a source cannot be used (no URL, not https, not a SHA-256, no files, a destination outside the game folder), or null.</summary>
    public static string? Problem(DependencySource source)
    {
        if (source.Urls is not { Count: > 0 }) return "there is no download URL";
        if (source.Urls.Any(address => !Uri.TryCreate(address, UriKind.Absolute, out var url) || url.Scheme != HttpsScheme)) return "a download is not an https URL";
        if (!IsSha256(source.Sha256)) return "the SHA-256 is not 64 hex digits";
        if (source.Files is not { Count: > 0 }) return "no files to take from the download";
        return source.Files.Any(file => string.IsNullOrWhiteSpace(file.From) || Inside(string.Empty, file.To) == null)
            ? "a file goes outside the game folder" : null;
    }

    /// <summary>A SHA-256 as 64 hex digits.</summary>
    internal static bool IsSha256(string hash) => hash.Length == Sha256HexLength && hash.All(Uri.IsHexDigit);

    /// <summary>Installs every unmet dependency of the game (a missing file, or another version than the accepted ones, which is archived
    /// first); true when none is unmet afterwards. A failure is reported with its reason (Failed) and returns false.
    /// <paramref name="open"/> replaces the HTTP download (tests).</summary>
    public static bool Install(GameProfile profile, string gameDir, IProgress<ModProgress>? progress, Opener? open = null)
    {
        var report = new ProgressReporter(progress);
        var unmet = profile.UnmetRequirements(gameDir);
        if (unmet.FirstOrDefault(problem => problem.Required.Install == null) is { } manual)
        {
            report.Fail($"{manual.Name} must be installed by hand");
            return false;
        }
        foreach (var problem in unmet)
        {
            var required = problem.Required;
            try
            {
                if (Fetch(required, open ?? Open, report, out var failure) is not { } zip)
                {
                    report.Fail(failure);
                    return false;
                }
                if (problem.OtherVersion) Archive(required, gameDir);
                Extract(zip, required.Install!, gameDir, report);
                Log.Info($"Installed {required.Name} into {gameDir}");
            }
            catch (Exception e) when (e is IOException or InvalidDataException or UnauthorizedAccessException)
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

    /// <summary>The verified zip of a dependency from the first of its URLs that works, or null with the words for the player in
    /// <paramref name="failure"/> (the raw errors go to the log).</summary>
    private static byte[]? Fetch(RequiredFile required, Opener open, ProgressReporter report, out string failure)
    {
        var source = required.Install!;
        failure = string.Format(DownloadFailedFormat, required.Name);
        foreach (var address in source.Urls)
        {
            try
            {
                var zip = Download(address, open, report);
                if (Convert.ToHexString(SHA256.HashData(zip)).Equals(source.Sha256, StringComparison.OrdinalIgnoreCase)) return zip;
                Log.Info($"{required.Name}: {address} does not match its SHA-256");
                failure = $"{required.Name} download does not match its SHA-256";
            }
            catch (Exception e) when (e is IOException or HttpRequestException or OperationCanceledException)
            {
                Log.Info($"{required.Name}: download from {address} failed: {e.Message}");
                failure = string.Format(DownloadFailedFormat, required.Name);
            }
        }
        return null;
    }

    private static byte[] Download(string address, Opener open, ProgressReporter report)
    {
        using var overall = new CancellationTokenSource(ReleaseFeed.DownloadTimeout);
        var body = open(address, overall.Token);
        using var owner = body.Owner;
        using var stream = body.Stream;
        using var buffer = new MemoryStream();
        report.Begin(ModStep.Downloading, body.Length, 1);
        var chunk = new byte[ChunkBytes];
        for (var read = ReleaseFeed.ReadChunk(stream, chunk, overall.Token); read > 0; read = ReleaseFeed.ReadChunk(stream, chunk, overall.Token))
        {
            buffer.Write(chunk, 0, read);
            report.AddBytes(read);
        }
        return buffer.ToArray();
    }

    /// <summary>Moves the player's own copy of a required file into its archive folder before the pinned one goes in.</summary>
    private static void Archive(RequiredFile required, string gameDir)
    {
        var folder = ConflictArchive.FolderFor(gameDir, ArchiveFolder, required.Name, DateTime.Now);
        ConflictArchive.MoveInto(Path.Combine(gameDir, required.File), Path.Combine(folder, required.File));
        Log.Event($"Moved {required.Name} to {Path.GetRelativePath(gameDir, folder)}");
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

    private static Body Open(string url, CancellationToken cancel)
    {
        var response = ReleaseFeed.Get(url, cancel);
        return new Body(response.Content.ReadAsStream(), response.Content.Headers.ContentLength ?? 0, response);
    }
}
