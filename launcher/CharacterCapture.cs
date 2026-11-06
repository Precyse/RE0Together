using System.Globalization;
using System.Text.RegularExpressions;
using CoopLauncher.Gui;

namespace CoopLauncher;

/// <summary>What the joining screen shows of a capture: running, closing the game (the outcome is known), done, or failed with the
/// reason in words; LogPath is the mod's log the failed row names. Closing is appended: older values keep their numbers.</summary>
public enum CaptureState { Running, Done, Failed, Closing }

/// <summary>The capture's state for the window.</summary>
public sealed record CaptureView(CaptureState State, string Reason = "", string LogPath = "");

/// <summary>
/// The capture launch: a guest whose picked character was never read by the adapter (<see cref="OptionSources.CharacterUnread"/>)
/// gets it read before its session game starts. The newest game slot of that character's world (the game's own save folder, or a
/// campaign's) is written to the adapter ini as <c>capture=&lt;slot&gt;</c> with <c>arisen=&lt;id&gt;</c>, the game is started; the
/// adapter reads the character at the title and writes <c>campaigns.captureResult</c> (<c>ok</c> or <c>failed: &lt;code&gt;: &lt;detail&gt;</c>);
/// then the game is closed (asked once, ended after <see cref="CloseWaitMs"/>, never waited for) and the capture ends
/// <see cref="SettleMs"/> after the game is gone, so the real launch never meets the dying process. Gives up after <see cref="TimeoutMs"/>,
/// or after <see cref="ModCheckMs"/> when the mod's logs show it never came up.
/// </summary>
public sealed partial class CharacterCapture
{
    public const long TimeoutMs = 180_000;
    public const long ModCheckMs = 45_000;
    public const long CloseWaitMs = 10_000;
    public const long SettleMs = 3_000;
    private const string OkResult = "ok";
    private const string FailedPrefix = "failed: ";
    private const string CodeSeparator = ": ";
    private const string TryAgain = "press READY to try again";
    private const string OpensWords = "{0} opens to its title and closes by itself";

    private readonly GameProfile _profile;
    private readonly string _gameDir;
    private readonly string _resultFile;
    private readonly long _startedMs;
    private readonly DateTime _startedUtc = DateTime.UtcNow;
    private CaptureView? _outcome;
    private long _closeRequestedMs;
    private long? _goneMs;
    private bool _killed;
    private bool _modChecked;

    public CaptureView View { get; private set; }

    /// <summary>The line the running row shows under its text.</summary>
    public static string OpensLine(GameProfile profile) => string.Format(OpensWords, profile.Name);

    private CharacterCapture(GameProfile profile, string gameDir, string resultFile, long startedMs)
    {
        _profile = profile;
        _gameDir = gameDir;
        _resultFile = resultFile;
        _startedMs = startedMs;
        View = new CaptureView(CaptureState.Running, LogPath: ModLoadCheck.LogPath(profile, gameDir));
    }

    /// <summary>The capture is still going (reading, or closing the game): nothing else may start the game.</summary>
    public bool InProgress => View.State is CaptureState.Running or CaptureState.Closing;

    /// <summary>Starts the capture of the character <paramref name="characterId"/> (a campaign id; empty: the game's own save). The
    /// returned capture is Failed at once when it cannot start (the game already runs, no save to read, Steam refused).</summary>
    public static CharacterCapture Start(GameProfile profile, string gameDir, string characterId, long nowMs)
    {
        var resultFile = Path.Combine(gameDir, profile.Campaigns!.CaptureResult);
        var capture = new CharacterCapture(profile, gameDir, resultFile, nowMs);
        var saveDir = characterId.Length == 0
            ? profile.SaveSync?.HostSaveDir is { } template ? SavePaths.Expand(template) : null
            : CampaignStore.SaveDir(profile, gameDir, characterId);
        if (GameLauncher.IsRunning(profile)) return capture.Fail(StartWords(LaunchResult.AlreadyRunning, profile, gameDir));
        var names = saveDir != null && Directory.Exists(saveDir) ? Directory.GetFiles(saveDir).Select(path => Path.GetFileName(path)) : [];
        if (saveDir == null || NewestSlot(names, name => File.GetLastWriteTimeUtc(Path.Combine(saveDir, name))) is not { } slot)
            return capture.Fail($"No {profile.Name} save on this PC: play once to create your Arisen, or pick another character");
        try
        {
            File.Delete(resultFile);
            AdapterSettings.WriteOptions(gameDir, profile, new Dictionary<string, string>
            {
                [AdapterSettings.CaptureKey] = slot.ToString(CultureInfo.InvariantCulture), [OptionSources.CharacterOptionKey(profile)!] = characterId,
                [AdapterSettings.CoopKey] = "0",  // a capture launch plays no session, whatever an earlier one left
            });
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            return capture.Fail($"The mod settings could not be written ({e.Message}), {TryAgain}");
        }
        Log.Info($"Reading the character of {(characterId.Length == 0 ? "the own save" : characterId)} from save slot {slot}");
        var launched = GameLauncher.Launch(profile, gameDir);
        return launched == LaunchResult.Started ? capture : capture.Fail(StartWords(launched, profile, gameDir));
    }

    /// <summary>Loop thread: reads the adapter's result once it is there, or gives up after the timeout, or when the mod's logs say it
    /// never came up; then closes the game and ends a moment after it is gone.</summary>
    public void Pump(long nowMs)
    {
        if (View.State == CaptureState.Closing) PumpClosing(nowMs);
        else if (View.State == CaptureState.Running) PumpRunning(nowMs);
    }

    /// <summary>Ends the capture now (the session was dropped under it): the game is asked to close and the ini is set back at once; the
    /// caller keeps pumping until <see cref="InProgress"/> is false.</summary>
    public void Abort(long nowMs)
    {
        if (View.State == CaptureState.Running) BeginClose(View with { State = CaptureState.Failed, Reason = "The capture was cancelled" }, nowMs);
    }

    /// <summary>The slot of the newest game save among file names: <c>data000.bin</c> is slot 0, <c>data00NSlot.bin</c> slot N; the
    /// system file (<c>data00-1.bin</c>) and anything else is not a game slot. Null when there is none (pure, unit tested).</summary>
    public static int? NewestSlot(IEnumerable<string> names, Func<string, DateTime>? writeTime = null) =>
        names.Select(name => (Name: name, Match: GameSlot().Match(name)))
            .Where(pair => pair.Match.Success)
            .OrderByDescending(pair => writeTime?.Invoke(pair.Name) ?? DateTime.MinValue)
            .Select(pair => (int?)int.Parse(pair.Match.Groups[1].Value, CultureInfo.InvariantCulture))
            .FirstOrDefault();

    /// <summary>The adapter's result line as a view: <c>ok</c> is done; <c>failed: &lt;code&gt;: &lt;detail&gt;</c> failed with a sentence for its
    /// code (title, load, empty, redirect, write, timeout) or, for a code not known, its detail; anything else failed as unreadable.</summary>
    public static CaptureView ViewOf(string result, string gameName = "the game")
    {
        if (result == OkResult) return new CaptureView(CaptureState.Done);
        if (!result.StartsWith(FailedPrefix, StringComparison.Ordinal)) return new CaptureView(CaptureState.Failed, $"The mod gave no readable answer, {TryAgain}");
        var rest = result[FailedPrefix.Length..];
        var split = rest.IndexOf(CodeSeparator, StringComparison.Ordinal);
        var code = split < 0 ? string.Empty : rest[..split];
        var detail = split < 0 ? rest : rest[(split + CodeSeparator.Length)..];
        return new CaptureView(CaptureState.Failed, ReasonOf(code, detail, gameName));
    }

    private static string ReasonOf(string code, string detail, string gameName) => code switch
    {
        "title" => $"{gameName} did not reach its title screen, {TryAgain}",
        "load" => $"The save could not be loaded in {gameName}, {TryAgain}",
        "empty" => $"That save has no character yet: load it in {gameName}, save once, then press READY",
        "redirect" => $"The mod could not protect your save folder, {TryAgain}",
        "write" => $"Your character could not be written, {TryAgain}",
        "timeout" => $"{gameName} took too long, {TryAgain}",
        _ => detail,
    };

    private static string StartWords(LaunchResult result, GameProfile profile, string gameDir) => result switch
    {
        LaunchResult.GameUnsupported => StatusText.Refusal(new Refusal(RefusalKind.GameUnsupported, GameBuilds.Installed(profile, gameDir).ToString(), profile.Id)),
        LaunchResult.ModNotInstalled => StatusText.Refusal(new Refusal(RefusalKind.ModNotInstalled, string.Empty, profile.Id)),
        LaunchResult.AlreadyRunning => StatusText.Refusal(new Refusal(RefusalKind.GameRunning, profile.Name, profile.Id)),
        _ => StatusText.Refusal(new Refusal(RefusalKind.LaunchFailed, string.Empty, profile.Id)),
    };

    private void PumpRunning(long nowMs)
    {
        string? result = null;
        try
        {
            if (File.Exists(_resultFile)) result = File.ReadAllText(_resultFile).Trim();
        }
        catch (IOException)
        {
            return;  // the adapter is still writing it
        }
        if (result != null) BeginClose(ViewOf(result, _profile.Name) with { LogPath = View.LogPath }, nowMs);
        else if (nowMs - _startedMs >= TimeoutMs) BeginClose(ViewOf($"{FailedPrefix}timeout{CodeSeparator}", _profile.Name) with { LogPath = View.LogPath }, nowMs);
        else if (!_modChecked && nowMs - _startedMs >= ModCheckMs) CheckModCame(nowMs);
    }

    /// <summary>Once, <see cref="ModCheckMs"/> after the start: a mod that never wrote to its log cannot answer, so the capture fails now with the cause.</summary>
    private void CheckModCame(long nowMs)
    {
        _modChecked = true;
        if (ModLoadCheck.Diagnose(_profile, _gameDir, _startedUtc) is { } cause)
            BeginClose(new CaptureView(CaptureState.Failed, StatusText.Refusal(cause), View.LogPath), nowMs);
    }

    private void PumpClosing(long nowMs)
    {
        if (GameCloser.IsGone(_profile))
        {
            _goneMs ??= nowMs;
            if (nowMs - _goneMs >= SettleMs) Complete();
            return;
        }
        _goneMs = null;
        if (_killed || nowMs - _closeRequestedMs < CloseWaitMs) return;
        _killed = true;
        GameCloser.Kill(_profile);
    }

    private CharacterCapture Fail(string reason)
    {
        View = View with { State = CaptureState.Failed, Reason = reason };
        Log.Info($"Character not read: {reason}");
        return this;
    }

    /// <summary>The outcome is known: the game is asked to close and the capture is taken out of the ini; the capture ends once the game is gone.</summary>
    private void BeginClose(CaptureView outcome, long nowMs)
    {
        _outcome = outcome;
        View = View with { State = CaptureState.Closing };
        _closeRequestedMs = nowMs;
        GameCloser.RequestClose(_profile);
        try
        {
            AdapterSettings.WriteOptions(_gameDir, _profile, new Dictionary<string, string> { [AdapterSettings.CaptureKey] = AdapterSettings.NoCapture });
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            Log.Info($"Could not clear the capture from the mod settings: {e.Message}");
        }
    }

    private void Complete()
    {
        View = _outcome!;
        Log.Info(View.State == CaptureState.Done ? "Character read" : $"Character not read: {View.Reason}");
    }

    [GeneratedRegex(@"^data0*(\d+)(?:Slot)?\.bin$", RegexOptions.IgnoreCase)]
    private static partial Regex GameSlot();
}
