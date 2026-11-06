using System.Globalization;

namespace CoopLauncher;

/// <summary>Where one player is in getting a session going; published to the lobby so everyone can show it. Ready: everything
/// is prepared and the launch waits for the start gate (<see cref="StartGate"/>). PickingCharacter: a guest that brings its
/// character has to pick it and press READY; CharacterNotRead: the capture launch could not read it (the reason is published).</summary>
public enum SessionStep { None, Lobby, CheckingBuild, ReceivingSave, StartingGame, InGame, GameClosed, Refused, Ready, ReadingCharacter, PickingCharacter, CharacterNotRead }

/// <summary>What the step depends on, read from the loop's state.</summary>
public sealed record StepInputs(
    bool LobbyReady, bool SessionStarted, bool Refused, bool BuildCheckPending, bool SaveSyncPending, bool LaunchPending,
    bool Bridged, bool BridgeReady, bool BridgeWasReady, bool StartHeld = false, bool ReadingCharacter = false,
    bool PickingCharacter = false, bool CharacterFailed = false);

public static class SessionProgress
{
    /// <summary>The characters and outfits can be changed until the game starts: from then on they are fixed.</summary>
    public static bool SelectionLocked(SessionStep step) => step is SessionStep.StartingGame or SessionStep.InGame or SessionStep.GameClosed;

    /// <summary>The host's campaign and save slot are fixed once a guest has begun reading its character from that world: from
    /// reading on, a guest's step is one of these.</summary>
    public static bool WorldCommitted(SessionStep guestStep) =>
        guestStep is SessionStep.ReadingCharacter or SessionStep.Ready or SessionStep.StartingGame or SessionStep.InGame or SessionStep.GameClosed;

    /// <summary>The first rule that applies, in this order. A session without the bridge (a rendezvous game's own transport)
    /// is in game as soon as it started: the game's own status line tells the rest.</summary>
    public static SessionStep Step(StepInputs inputs)
    {
        if (inputs.Refused) return SessionStep.Refused;
        if (!inputs.LobbyReady) return SessionStep.None;
        if (!inputs.SessionStarted) return SessionStep.Lobby;
        if (inputs.BuildCheckPending) return SessionStep.CheckingBuild;
        if (inputs.SaveSyncPending) return SessionStep.ReceivingSave;
        if (inputs.LaunchPending && inputs.ReadingCharacter) return SessionStep.ReadingCharacter;
        if (inputs.LaunchPending && inputs.CharacterFailed) return SessionStep.CharacterNotRead;
        if (inputs.LaunchPending && inputs.PickingCharacter) return SessionStep.PickingCharacter;
        if (inputs.LaunchPending && inputs.StartHeld) return SessionStep.Ready;
        if (inputs.LaunchPending) return SessionStep.StartingGame;
        if (!inputs.Bridged) return SessionStep.InGame;
        if (inputs.BridgeReady) return SessionStep.InGame;
        return inputs.BridgeWasReady ? SessionStep.GameClosed : SessionStep.StartingGame;
    }
}

/// <summary>Whether a window launcher may start its game: the host once it asked to (START GAME), a guest once the host's game
/// is starting or running. A launcher without a held start (a command-line run, a game that already runs) is always open.</summary>
public static class StartGate
{
    /// <summary><paramref name="latched"/>: the host was seen starting earlier in this session, so a host that blinks does not
    /// close the gate again. <paramref name="guestConfirms"/>: a guest of a game where it picks a character starts only once it
    /// pressed READY (<paramref name="requested"/>) as well.</summary>
    public static bool Open(bool isHost, bool held, bool requested, SessionStep hostStep, bool latched, bool guestConfirms = false)
    {
        if (!held) return true;
        return isHost ? requested : (requested || !guestConfirms) && (latched || HostStarted(hostStep));
    }

    public static bool HostStarted(SessionStep hostStep) => hostStep is SessionStep.StartingGame or SessionStep.InGame;
}

/// <summary>What a player publishes about itself: its step with the percent of the save (ReceivingSave), the refusal (Refused: its
/// kind and its detail) or the reason a character could not be read (CharacterNotRead), a label (the host's campaign, a guest's
/// character) and what the adapter says about the game (<see cref="GameState"/>).</summary>
public sealed record StepReport(
    SessionStep Step, int Percent = 0, RefusalKind? Refusal = null, string Detail = "", string Label = "", GameState? Game = null)
{
    public static readonly StepReport None = new(SessionStep.None);
}

/// <summary>The lobby member-data text of a step: <c>step=ReceivingSave;pct=40</c>, <c>step=Refused;why=ModFiles;detail=3</c>,
/// <c>step=CharacterNotRead;why=no save</c>, <c>step=InGame;label=Arisen / Lv 4;place=world;char=ok;save=ok;slot=2</c>.</summary>
public static class StepCodec
{
    private const char PairSeparator = ';';
    private const char KeyValueSeparator = '=';
    private const string StepKey = "step";
    private const string PercentKey = "pct";
    private const string WhyKey = "why";
    private const string DetailKey = "detail";
    private const string LabelKey = "label";
    private const int MaxPercent = 100;
    private const int MaxTextLength = 120;

    public static string Encode(StepReport report)
    {
        var pairs = new List<string> { $"{StepKey}{KeyValueSeparator}{report.Step}" };
        if (report.Step == SessionStep.ReceivingSave) pairs.Add($"{PercentKey}{KeyValueSeparator}{Math.Clamp(report.Percent, 0, MaxPercent)}");
        if (report.Step == SessionStep.Refused && report.Refusal is { } kind)
        {
            pairs.Add($"{WhyKey}{KeyValueSeparator}{kind}");
            if (report.Detail.Length > 0) pairs.Add($"{DetailKey}{KeyValueSeparator}{Clean(report.Detail)}");
        }
        if (report.Step == SessionStep.CharacterNotRead) pairs.Add($"{WhyKey}{KeyValueSeparator}{Clean(report.Detail)}");
        if (report.Label.Length > 0) pairs.Add($"{LabelKey}{KeyValueSeparator}{Clean(report.Label)}");
        if (report.Game != null) pairs.AddRange(report.Game.EncodePairs());
        return string.Join(PairSeparator, pairs);
    }

    /// <summary>False for empty or unknown text (a member on another launcher build publishes nothing or something else).</summary>
    public static bool TryDecode(string text, out StepReport report)
    {
        report = StepReport.None;
        var pairs = GameState.Pairs(text);
        if (!pairs.TryGetValue(StepKey, out var name) || !TryEnum(name, out SessionStep step)) return false;
        var percent = pairs.TryGetValue(PercentKey, out var pct) && int.TryParse(pct, NumberStyles.None, CultureInfo.InvariantCulture, out var value)
            ? Math.Min(value, MaxPercent) : 0;
        RefusalKind? refusal = null;
        var detail = pairs.GetValueOrDefault(DetailKey, string.Empty);
        if (step == SessionStep.Refused && pairs.TryGetValue(WhyKey, out var why) && TryEnum(why, out RefusalKind kind)) refusal = kind;
        if (step == SessionStep.CharacterNotRead) detail = pairs.GetValueOrDefault(WhyKey, string.Empty);
        var game = GameState.FromPairs(pairs);
        report = new StepReport(step, percent, refusal, detail, pairs.GetValueOrDefault(LabelKey, string.Empty), game.IsEmpty ? null : game);
        return true;
    }

    /// <summary>Free text must not hold the separators, and stays short.</summary>
    private static string Clean(string text)
    {
        var clean = text.Replace(PairSeparator, ',').Replace(KeyValueSeparator, ':');
        return clean.Length > MaxTextLength ? clean[..MaxTextLength] : clean;
    }

    private static bool TryEnum<T>(string name, out T value) where T : struct, Enum
    {
        value = default;
        return name.Length > 0 && !char.IsDigit(name[0]) && Enum.TryParse(name, out value) && Enum.IsDefined(value);
    }
}

/// <summary>How much of the host's save a guest has received: bytes arrived over the sizes announced so far, and the file that
/// last received a chunk. Fed from the
/// FILE_BEGIN and FILE_CHUNK frames the session passes on; 0 before the first FILE_BEGIN. A file the host sends again (a
/// failed transfer, a rejoin) under a new transfer id replaces its earlier transfer instead of adding to the total.</summary>
public sealed class SaveProgressMeter
{
    private const int PercentScale = 100;

    private readonly Dictionary<uint, long> _sizes = new();
    private readonly Dictionary<uint, long> _received = new();
    private readonly Dictionary<string, uint> _transferOf = new();
    private readonly Dictionary<uint, string> _nameOf = new();
    private uint? _current;

    public int Percent
    {
        get
        {
            var total = TotalBytes;
            return total == 0 ? 0 : (int)Math.Min(PercentScale, ReceivedBytes * PercentScale / total);
        }
    }

    /// <summary>The file whose chunk arrived last; empty before the first chunk.</summary>
    public string CurrentFile => _current is { } id && _nameOf.TryGetValue(id, out var name) ? name : string.Empty;

    public long ReceivedBytes => _received.Values.Sum();

    /// <summary>The sizes of every file announced so far.</summary>
    public long TotalBytes => _sizes.Values.Sum();

    /// <summary>A file starts (or starts again): its size joins the total and its received bytes restart; an earlier transfer of
    /// the same file no longer counts.</summary>
    public void OnBegin(uint id, string name, uint size)
    {
        if (_transferOf.Remove(name, out var earlier))
        {
            _sizes.Remove(earlier);
            _received.Remove(earlier);
            _nameOf.Remove(earlier);
            if (_current == earlier) _current = null;
        }
        _transferOf[name] = id;
        _nameOf[id] = name;
        _sizes[id] = size;
        _received[id] = 0;
    }

    public void OnChunk(uint id, int bytes)
    {
        if (!_sizes.ContainsKey(id)) return;
        _received[id] += bytes;
        _current = id;
    }
}
