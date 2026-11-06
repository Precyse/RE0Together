using CoopLauncher.Gui;

namespace CoopLauncher;

/// <summary>Why a game that was started did not bring its co-op mod up, worked out from the logs of the mod loader and of the mod:
/// the loader's own log not written since the start (it did not load), the mod's log not written since (the mod did not load), or
/// the mod's log saying it left the game alone ("co-op off": an unsupported game build, another co-op mod, any other reason it names).</summary>
public static class ModLoadCheck
{
    /// <summary>The text any adapter writes when it leaves the game alone; what follows it (after a colon) is the reason it gives.</summary>
    private const string InertMarker = "co-op off";
    private const string BuildMarkerSpaced = "stamp 0x";
    private const string BuildMarkerEquals = "stamp=0x";
    private const string ConflictMarker = "conflict: ";
    private const string ConflictPresent = " present";
    private const string WaitingMarker = "waiting for the title";
    private const string ReachedMarker = "WorldSystem present";
    private const string LineTimeFormat = "yyyy-MM-dd HH:mm:ss.fff";
    private static readonly char[] ReasonTrim = [':', ' ', '	'];
    private static readonly TimeSpan ClockSlack = TimeSpan.FromSeconds(2);

    /// <summary>What the mod's log says since the start: the line on which it left the game alone (null when it did not), and whether it
    /// waits for the game's title screen (it started, and has not seen the title yet).</summary>
    private sealed record LogFacts(string? InertLine = null, bool WaitingForTitle = false);

    /// <summary>The refusal that explains it, or null when both logs say the mod came up (the cause is then elsewhere). The log
    /// timestamps are compared with <paramref name="startedUtc"/>, when the game was started.</summary>
    public static Refusal? Diagnose(GameProfile profile, string gameDir, DateTime startedUtc)
    {
        startedUtc -= ClockSlack;
        if (profile.FrameworkLog is { } loaderLog && !WrittenSince(Path.Combine(gameDir, loaderLog), startedUtc))
            return new Refusal(RefusalKind.FrameworkNotLoaded, profile.Requires?.FirstOrDefault()?.Name ?? string.Empty, profile.Id);
        if (profile.AdapterLog is not { } modLog) return null;
        var path = Path.Combine(gameDir, modLog);
        if (!WrittenSince(path, startedUtc)) return new Refusal(RefusalKind.ModNotLoaded, string.Empty, profile.Id);
        return ReadLog(path, startedUtc).InertLine is { } line ? InertRefusal(line, profile, gameDir) : null;
    }

    /// <summary>The mod started and its log says it waits for the title screen (the game has not got there): not a failure.</summary>
    public static bool WaitingForTitle(GameProfile profile, string gameDir, DateTime startedUtc)
    {
        if (profile.AdapterLog is not { } modLog) return false;
        var path = Path.Combine(gameDir, modLog);
        startedUtc -= ClockSlack;
        return WrittenSince(path, startedUtc) && ReadLog(path, startedUtc).WaitingForTitle;
    }

    /// <summary>The path of the mod's log, for the line under the failure.</summary>
    public static string LogPath(GameProfile profile, string gameDir) => profile.AdapterLog is { } log ? Path.Combine(gameDir, log) : string.Empty;

    private static bool WrittenAfter(string line, DateTime since) =>
        line.Length >= LineTimeFormat.Length
        && DateTime.TryParseExact(line[..LineTimeFormat.Length], LineTimeFormat, System.Globalization.CultureInfo.InvariantCulture, System.Globalization.DateTimeStyles.None, out var written)
        && written >= since;

    private static bool WrittenSince(string path, DateTime startedUtc) => File.Exists(path) && File.GetLastWriteTimeUtc(path) >= startedUtc;

    /// <summary>The refusal for the line on which the mod left the game alone: an unsupported game build, another co-op mod it names
    /// ("conflict: &lt;name&gt; present"), or the reason it gives after the marker.</summary>
    private static Refusal InertRefusal(string line, GameProfile profile, string gameDir)
    {
        if (line.Contains(BuildMarkerSpaced, StringComparison.Ordinal) || line.Contains(BuildMarkerEquals, StringComparison.Ordinal))
            return new Refusal(RefusalKind.GameUnsupported, GameBuilds.Installed(profile, gameDir).ToString(), profile.Id);
        var conflict = line.IndexOf(ConflictMarker, StringComparison.Ordinal);
        var present = conflict < 0 ? -1 : line.IndexOf(ConflictPresent, conflict, StringComparison.Ordinal);
        if (present > 0)
        {
            var start = conflict + ConflictMarker.Length;
            return new Refusal(RefusalKind.OtherCoopMod, line[start..present], profile.Id);
        }
        var reason = line[(line.IndexOf(InertMarker, StringComparison.Ordinal) + InertMarker.Length)..].Trim(ReasonTrim);
        return new Refusal(RefusalKind.ModNotStarted, reason, profile.Id);
    }

    /// <summary>Only the lines written since the start count: the log keeps the lines of earlier runs.</summary>
    private static LogFacts ReadLog(string path, DateTime startedUtc)
    {
        var since = startedUtc.ToLocalTime();
        string? inert = null;
        var waiting = false;
        try
        {
            using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
            using var reader = new StreamReader(stream);
            while (reader.ReadLine() is { } line)
            {
                if (!WrittenAfter(line, since)) continue;
                if (line.Contains(InertMarker, StringComparison.Ordinal)) inert ??= line;
                if (line.Contains(WaitingMarker, StringComparison.Ordinal)) waiting = true;
                else if (line.Contains(ReachedMarker, StringComparison.Ordinal)) waiting = false;
            }
        }
        catch (IOException)
        {
            return new LogFacts();
        }
        return new LogFacts(inert, waiting);
    }
}
