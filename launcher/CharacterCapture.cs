using System.Diagnostics;
using System.Globalization;
using System.Text.RegularExpressions;

namespace CoopLauncher;

/// <summary>What the joining screen shows of a capture: running, done, or failed with the reason the adapter gave.</summary>
public enum CaptureState { Running, Done, Failed }

/// <summary>The capture's state for the window.</summary>
public sealed record CaptureView(CaptureState State, string Reason = "");

/// <summary>
/// The capture launch: a guest whose picked character was never read by the adapter (<see cref="OptionSources.CharacterUnread"/>)
/// gets it read before its session game starts. The newest game slot of that character's world (the game's own save folder, or a
/// campaign's) is written to the adapter ini as <c>capture=&lt;slot&gt;</c> with <c>arisen=&lt;id&gt;</c>, the game is started; the
/// adapter reads the character at the title and writes <c>campaigns.captureResult</c> ("ok" or "failed: &lt;why&gt;"); then the
/// game is closed and the ini set back. Gives up after <see cref="TimeoutMs"/>.
/// </summary>
public sealed partial class CharacterCapture
{
    public const long TimeoutMs = 180_000;
    private const long CloseWaitMs = 10_000;
    private const string CaptureKey = "capture";
    private const string NoCapture = "-1";
    private const string OkResult = "ok";
    private const string FailedPrefix = "failed: ";

    private readonly GameProfile _profile;
    private readonly string _gameDir;
    private readonly string _resultFile;
    private readonly long _startedMs;

    public CaptureView View { get; private set; } = new(CaptureState.Running);

    private CharacterCapture(GameProfile profile, string gameDir, string resultFile, long startedMs)
    {
        _profile = profile;
        _gameDir = gameDir;
        _resultFile = resultFile;
        _startedMs = startedMs;
    }

    /// <summary>Starts the capture of the character <paramref name="characterId"/> (a campaign id; empty: the game's own save). The
    /// returned capture is Failed at once when it cannot start (the game already runs, no save to read, Steam refused).</summary>
    public static CharacterCapture Start(GameProfile profile, string gameDir, string characterId, long nowMs)
    {
        var resultFile = Path.Combine(gameDir, profile.Campaigns!.CaptureResult);
        var capture = new CharacterCapture(profile, gameDir, resultFile, nowMs);
        var saveDir = characterId.Length == 0
            ? profile.SaveSync?.HostSaveDir is { } template ? SavePaths.Expand(template) : null
            : CampaignStore.SaveDir(profile, gameDir, characterId);
        if (GameLauncher.IsRunning(profile)) return capture.Fail($"close {profile.Name} first, then press READY again");
        var names = saveDir != null && Directory.Exists(saveDir) ? Directory.GetFiles(saveDir).Select(path => Path.GetFileName(path)) : [];
        if (saveDir == null || NewestSlot(names, name => File.GetLastWriteTimeUtc(Path.Combine(saveDir, name))) is not { } slot)
            return capture.Fail("no save found to read the character from");
        try
        {
            File.Delete(resultFile);
            AdapterSettings.WriteOptions(gameDir, profile, new Dictionary<string, string>
            {
                [CaptureKey] = slot.ToString(CultureInfo.InvariantCulture), [OptionSources.CharacterOptionKey(profile)!] = characterId,
                [AdapterSettings.CoopKey] = "0",  // a capture launch plays no session, whatever an earlier one left
            });
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            return capture.Fail($"the mod settings could not be written ({e.Message})");
        }
        Log.Info($"Reading the character of {(characterId.Length == 0 ? "the own save" : characterId)} from save slot {slot}");
        if (GameLauncher.Launch(profile, gameDir) != LaunchResult.Started) return capture.Finish($"{FailedPrefix}{profile.Name} could not be started");
        return capture;
    }

    /// <summary>Loop thread: reads the adapter's result once it is there, or gives up after the timeout; either way closes the game.</summary>
    public void Pump(long nowMs)
    {
        if (View.State != CaptureState.Running) return;
        string? result = null;
        try
        {
            if (File.Exists(_resultFile)) result = File.ReadAllText(_resultFile).Trim();
        }
        catch (IOException)
        {
            return;  // the adapter is still writing it
        }
        if (result == null && nowMs - _startedMs < TimeoutMs) return;
        Finish(result ?? $"{FailedPrefix}{_profile.Name} did not reach its title screen in time");
    }

    /// <summary>The slot of the newest game save among file names: <c>data000.bin</c> is slot 0, <c>data00NSlot.bin</c> slot N; the
    /// system file (<c>data00-1.bin</c>) and anything else is not a game slot. Null when there is none (pure, unit tested).</summary>
    public static int? NewestSlot(IEnumerable<string> names, Func<string, DateTime>? writeTime = null) =>
        names.Select(name => (Name: name, Match: GameSlot().Match(name)))
            .Where(pair => pair.Match.Success)
            .OrderByDescending(pair => writeTime?.Invoke(pair.Name) ?? DateTime.MinValue)
            .Select(pair => (int?)int.Parse(pair.Match.Groups[1].Value, CultureInfo.InvariantCulture))
            .FirstOrDefault();

    /// <summary>The adapter's result line as a view: "ok" is done, "failed: why" failed with why, anything else failed as unreadable.</summary>
    public static CaptureView ViewOf(string result) =>
        result == OkResult ? new CaptureView(CaptureState.Done)
        : result.StartsWith(FailedPrefix, StringComparison.Ordinal) ? new CaptureView(CaptureState.Failed, result[FailedPrefix.Length..])
        : new CaptureView(CaptureState.Failed, "the mod gave no readable answer");

    private CharacterCapture Fail(string reason)
    {
        View = new CaptureView(CaptureState.Failed, reason);
        Log.Info($"Character not read: {reason}");
        return this;
    }

    private CharacterCapture Finish(string result)
    {
        View = ViewOf(result);
        Log.Info(View.State == CaptureState.Done ? "Character read" : $"Character not read: {View.Reason}");
        CloseGame();
        try
        {
            AdapterSettings.WriteOptions(_gameDir, _profile, new Dictionary<string, string> { [CaptureKey] = NoCapture });
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            Log.Info($"Could not clear the capture from the mod settings: {e.Message}");
        }
        return this;
    }

    /// <summary>The game was started only for the capture: asked to close, then ended when it does not within <see cref="CloseWaitMs"/>.</summary>
    private void CloseGame()
    {
        foreach (var process in Process.GetProcessesByName(Path.GetFileNameWithoutExtension(_profile.Exe)))
        {
            using (process)
            {
                if (process.CloseMainWindow() && process.WaitForExit((int)CloseWaitMs)) continue;
                if (!process.HasExited) process.Kill();
            }
        }
    }

    [GeneratedRegex(@"^data0*(\d+)(?:Slot)?\.bin$", RegexOptions.IgnoreCase)]
    private static partial Regex GameSlot();
}
