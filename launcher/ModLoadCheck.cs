using CoopLauncher.Gui;

namespace CoopLauncher;

/// <summary>Why a game that was started did not bring its co-op mod up, worked out from the logs of the mod loader and of the mod:
/// the loader's own log not written since the start (it did not load), the mod's log not written since (the mod did not load), or
/// the mod's log saying it switched itself off for this game build.</summary>
public static class ModLoadCheck
{
    /// <summary>The line the mod writes when it leaves the game alone (an unsupported game build or another co-op mod).</summary>
    private const string InertMarker = "co-op off";
    private const string BuildMarker = "stamp 0x";
    private const string LineTimeFormat = "yyyy-MM-dd HH:mm:ss.fff";
    private static readonly TimeSpan ClockSlack = TimeSpan.FromSeconds(2);

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
        return SwitchedItselfOff(path, startedUtc) ? new Refusal(RefusalKind.GameUnsupported, GameBuilds.Installed(profile, gameDir).ToString(), profile.Id) : null;
    }

    /// <summary>The path of the mod's log, for the line under the failure.</summary>
    public static string LogPath(GameProfile profile, string gameDir) => profile.AdapterLog is { } log ? Path.Combine(gameDir, log) : string.Empty;

    private static bool WrittenAfter(string line, DateTime since) =>
        line.Length >= LineTimeFormat.Length
        && DateTime.TryParseExact(line[..LineTimeFormat.Length], LineTimeFormat, System.Globalization.CultureInfo.InvariantCulture, System.Globalization.DateTimeStyles.None, out var written)
        && written >= since;

    private static bool WrittenSince(string path, DateTime startedUtc) => File.Exists(path) && File.GetLastWriteTimeUtc(path) >= startedUtc;

    /// <summary>Only the lines written since the start count: the log keeps the lines of earlier runs.</summary>
    private static bool SwitchedItselfOff(string path, DateTime startedUtc)
    {
        var since = startedUtc.ToLocalTime();
        try
        {
            using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
            using var reader = new StreamReader(stream);
            while (reader.ReadLine() is { } line)
                if (line.Contains(InertMarker, StringComparison.Ordinal) && line.Contains(BuildMarker, StringComparison.Ordinal) && WrittenAfter(line, since)) return true;
        }
        catch (IOException)
        {
            return false;
        }
        return false;
    }
}
