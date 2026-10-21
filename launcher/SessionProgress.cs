using System.Globalization;

namespace CoopLauncher;

/// <summary>Where one player is in getting a session going; published to the lobby so everyone can show it.</summary>
public enum SessionStep { None, Lobby, CheckingBuild, ReceivingSave, StartingGame, InGame, GameClosed, Refused }

/// <summary>What the step depends on, read from the loop's state.</summary>
public sealed record StepInputs(
    bool LobbyReady, bool SessionStarted, bool Refused, bool BuildCheckPending, bool SaveSyncPending, bool LaunchPending,
    bool Bridged, bool BridgeReady, bool BridgeWasReady);

public static class SessionProgress
{
    /// <summary>The first rule that applies, in this order. A session without the bridge (a rendezvous game's own transport)
    /// is in game as soon as it started: the game's own status line tells the rest.</summary>
    public static SessionStep Step(StepInputs inputs)
    {
        if (inputs.Refused) return SessionStep.Refused;
        if (!inputs.LobbyReady) return SessionStep.None;
        if (!inputs.SessionStarted) return SessionStep.Lobby;
        if (inputs.BuildCheckPending) return SessionStep.CheckingBuild;
        if (inputs.SaveSyncPending) return SessionStep.ReceivingSave;
        if (inputs.LaunchPending) return SessionStep.StartingGame;
        if (!inputs.Bridged) return SessionStep.InGame;
        if (inputs.BridgeReady) return SessionStep.InGame;
        return inputs.BridgeWasReady ? SessionStep.GameClosed : SessionStep.StartingGame;
    }
}

/// <summary>The lobby member-data text of a step: <c>step=ReceivingSave;pct=40</c>, <c>step=Refused;why=ModFiles</c>.</summary>
public static class StepCodec
{
    private const char PairSeparator = ';';
    private const char KeyValueSeparator = '=';
    private const string StepKey = "step";
    private const string PercentKey = "pct";
    private const string RefusalKey = "why";
    private const int MaxPercent = 100;

    public static string Encode(SessionStep step, int percent, RefusalKind? refusal)
    {
        var text = $"{StepKey}{KeyValueSeparator}{step}";
        if (step == SessionStep.ReceivingSave) text += $"{PairSeparator}{PercentKey}{KeyValueSeparator}{Math.Clamp(percent, 0, MaxPercent)}";
        if (step == SessionStep.Refused && refusal is { } kind) text += $"{PairSeparator}{RefusalKey}{KeyValueSeparator}{kind}";
        return text;
    }

    /// <summary>False for empty or unknown text (a member on another launcher build publishes nothing or something else).</summary>
    public static bool TryDecode(string text, out SessionStep step, out int percent, out RefusalKind? refusal)
    {
        step = SessionStep.None;
        percent = 0;
        refusal = null;
        var pairs = text.Split(PairSeparator, StringSplitOptions.RemoveEmptyEntries)
            .Select(pair => pair.Split(KeyValueSeparator, 2)).Where(parts => parts.Length == 2)
            .GroupBy(parts => parts[0]).ToDictionary(group => group.Key, group => group.Last()[1]);
        if (!pairs.TryGetValue(StepKey, out var name) || !TryEnum(name, out step)) return false;
        if (pairs.TryGetValue(PercentKey, out var pct) && int.TryParse(pct, NumberStyles.None, CultureInfo.InvariantCulture, out var value))
            percent = Math.Min(value, MaxPercent);
        if (pairs.TryGetValue(RefusalKey, out var why) && TryEnum(why, out RefusalKind kind)) refusal = kind;
        return true;
    }

    private static bool TryEnum<T>(string name, out T value) where T : struct, Enum
    {
        value = default;
        return name.Length > 0 && !char.IsDigit(name[0]) && Enum.TryParse(name, out value) && Enum.IsDefined(value);
    }
}

/// <summary>How much of the host's save a guest has received: bytes arrived over the sizes announced so far. Fed from the
/// FILE_BEGIN and FILE_CHUNK frames the session passes on; 0 before the first FILE_BEGIN.</summary>
public sealed class SaveProgressMeter
{
    private const int PercentScale = 100;

    private readonly Dictionary<uint, long> _sizes = new();
    private readonly Dictionary<uint, long> _received = new();

    public int Percent
    {
        get
        {
            var total = _sizes.Values.Sum();
            return total == 0 ? 0 : (int)Math.Min(PercentScale, _received.Values.Sum() * PercentScale / total);
        }
    }

    /// <summary>A file starts (or starts again): its size joins the total and its received bytes restart.</summary>
    public void OnBegin(uint id, uint size)
    {
        _sizes[id] = size;
        _received[id] = 0;
    }

    public void OnChunk(uint id, int bytes)
    {
        if (_sizes.ContainsKey(id)) _received[id] += bytes;
    }
}
