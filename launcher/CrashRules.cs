using System.Text.RegularExpressions;

namespace CoopLauncher;

/// <summary>A crash file as the guest sees it on disk.</summary>
public readonly record struct CrashFileInfo(string Path, DateTime ModifiedUtc, long Length);

/// <summary>The files <see cref="CrashQueue.Take"/> hands over now, and those it dropped for their size.</summary>
public readonly record struct CrashScan(List<string> Ready, List<string> TooLarge);

/// <summary>Where a crash incident goes: the stamp names its folder, <see cref="Opened"/> marks the moment it opened,
/// <see cref="SendLog"/> says the guest's whole adapter log joins it now.</summary>
public readonly record struct CrashDecision(string Stamp, bool Opened, bool SendLog);

/// <summary>Names and places of the crash files a guest sends the host (<see cref="CrashSender"/>, <see cref="CrashReceiver"/>).
/// A file travels as <c>&lt;stamp&gt;/&lt;flat name&gt;</c> in FILE_BEGIN: the stamp (<see cref="AppData.StampFormat"/>) groups the files
/// of one incident, the flat name is the file's path in the game folder with the separators replaced.</summary>
public static partial class CrashRules
{
    /// <summary>The largest crash file sent (the same limit as a save file).</summary>
    public const long MaxFileBytes = SaveReceiver.MaxFileBytes;

    /// <summary>Incidents sent per session; the host takes no more folders from one peer.</summary>
    public const int MaxIncidents = 3;

    public const string PeerFolder = "peer_crash";

    private const string CoopFolder = "coop";
    private const char WireSeparator = '/';
    private const char FlatSeparator = '_';

    /// <summary>The host's folder for one incident of a peer: <c>&lt;game&gt;\coop\peer_crash\&lt;steamid&gt;_&lt;stamp&gt;</c>.</summary>
    public static string Destination(string gameDir, ulong peer, string stamp) =>
        Path.Combine(gameDir, CoopFolder, PeerFolder, $"{peer}_{stamp}");

    /// <summary>A profile entry (<c>coop/dumps/*.dmp</c>) as its folder and file pattern; false when it names no pattern.</summary>
    public static bool TrySplitSpec(string spec, out string directory, out string pattern)
    {
        var cut = spec.LastIndexOfAny(['/', '\\']);
        directory = cut < 0 ? "" : spec[..cut];
        pattern = spec[(cut + 1)..];
        return pattern.Length > 0;
    }

    public static string FlatName(string relativePath) =>
        relativePath.Replace('/', FlatSeparator).Replace('\\', FlatSeparator);

    public static string WireName(string stamp, string flatName) => stamp + WireSeparator + flatName;

    /// <summary>Splits a wire name; false unless the stamp has the stamp format and the file part is a plain file name.</summary>
    public static bool TryParseWireName(string wireName, out string stamp, out string file)
    {
        stamp = file = "";
        var cut = wireName.IndexOf(WireSeparator);
        if (cut < 0) return false;
        stamp = wireName[..cut];
        file = wireName[(cut + 1)..];
        return StampShape().IsMatch(stamp) && file.Length > 0 && Path.GetFileName(file) == file;
    }

    [GeneratedRegex(@"^\d{8}-\d{6}$")]
    private static partial Regex StampShape();
}

/// <summary>Which crash files are ready to send: modified after the session started, not yet taken, and unchanged in size for
/// <see cref="StableMs"/> (the game may still be writing a dump).</summary>
public sealed class CrashQueue(DateTime sessionStartUtc)
{
    public const long StableMs = 2000;

    private readonly Dictionary<string, (long Length, long SinceMs)> _watching = [];
    private readonly HashSet<(string Path, long ModifiedTicks)> _taken = [];

    /// <summary>The files to send now; each version of a file (path and modified time) is handed over once. A file over
    /// <see cref="CrashRules.MaxFileBytes"/> is reported as too large and never sent; an empty file waits for content.</summary>
    public CrashScan Take(long nowMs, IEnumerable<CrashFileInfo> files)
    {
        var scan = new CrashScan([], []);
        var seen = new HashSet<string>();
        foreach (var file in files.Where(f => f.ModifiedUtc >= sessionStartUtc && !_taken.Contains((f.Path, f.ModifiedUtc.Ticks))))
        {
            seen.Add(file.Path);
            if (!_watching.TryGetValue(file.Path, out var last) || last.Length != file.Length)
            {
                _watching[file.Path] = (file.Length, nowMs);
                continue;
            }
            if (file.Length == 0 || nowMs - last.SinceMs < StableMs) continue;
            _taken.Add((file.Path, file.ModifiedUtc.Ticks));
            (file.Length > CrashRules.MaxFileBytes ? scan.TooLarge : scan.Ready).Add(file.Path);
        }
        foreach (var gone in _watching.Keys.Where(path => !seen.Contains(path)).ToList()) _watching.Remove(gone);
        return scan;
    }
}

/// <summary>Which crash incident a moment belongs to. An incident opens on the first stable crash file or when the game is gone
/// (exited after it ran, or its window silent for <see cref="StalledAfterMs"/>) and takes everything that follows for
/// <see cref="WindowMs"/>; at most <see cref="CrashRules.MaxIncidents"/> open per session. The adapter log is sent once per
/// incident, as soon as the game is gone.</summary>
public sealed class CrashIncidents(Func<string> newStamp)
{
    public const long StalledAfterMs = 15_000;
    public const long WindowMs = 60_000;

    private bool _seenRunning;
    private bool _wasGone;
    private long? _silentSinceMs;
    private int _opened;
    private long _openedAtMs;
    private string? _stamp;
    private bool _logSent;

    /// <summary>True once the cap stopped an incident that had something to send.</summary>
    public bool CapReached { get; private set; }

    /// <summary>The incident this moment feeds, or null when there is none (nothing to send, or the cap is reached).</summary>
    public CrashDecision? Next(long nowMs, bool filesReady, GameProcess process)
    {
        _seenRunning |= process != GameProcess.Gone;
        _silentSinceMs = process == GameProcess.NotResponding ? _silentSinceMs ?? nowMs : null;
        var gone = IsGone(nowMs, process);
        var goneNow = gone && !_wasGone;
        _wasGone = gone;
        var opened = _stamp == null || nowMs - _openedAtMs > WindowMs;
        if (opened)
        {
            if (!filesReady && !goneNow) return null;
            if (_opened >= CrashRules.MaxIncidents)
            {
                CapReached = true;
                return null;
            }
            _opened++;
            _stamp = newStamp();
            _openedAtMs = nowMs;
            _logSent = false;
        }
        var sendLog = gone && !_logSent;
        _logSent |= sendLog;
        return new CrashDecision(_stamp!, opened, sendLog);
    }

    private bool IsGone(long nowMs, GameProcess process) =>
        process == GameProcess.Gone ? _seenRunning : _silentSinceMs is { } since && nowMs - since >= StalledAfterMs;
}
