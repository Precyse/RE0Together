using CoopLauncher;

namespace LauncherTests;

/// <summary>SessionProgress.Step (the first rule that applies), StepCodec (what a member publishes) and SaveProgressMeter.</summary>
public static class SessionProgressTests
{
    private static readonly StepInputs Started = new(
        LobbyReady: true, SessionStarted: true, Refused: false, BuildCheckPending: false, SaveSyncPending: false, LaunchPending: false,
        Bridged: false, BridgeReady: false, BridgeWasReady: false);

    public static void Run()
    {
        Steps();
        Gate();
        Codec();
        Meter();
    }

    private static void Steps()
    {
        Is(Started with { Refused = true, LobbyReady = false }, SessionStep.Refused, "a refusal wins over everything");
        Is(Started with { LobbyReady = false }, SessionStep.None, "no lobby yet: no step");
        Is(Started with { SessionStarted = false }, SessionStep.Lobby, "a ready lobby before the session starts: in lobby");
        Is(Started with { BuildCheckPending = true, SaveSyncPending = true, LaunchPending = true }, SessionStep.CheckingBuild, "build check comes first");
        Is(Started with { SaveSyncPending = true, LaunchPending = true }, SessionStep.ReceivingSave, "then the save");
        Is(Started with { LaunchPending = true, Bridged = true, BridgeReady = true }, SessionStep.StartingGame, "then the launch");
        Is(Started with { LaunchPending = true, StartHeld = true }, SessionStep.Ready, "a launch held by the start gate: ready");
        Is(Started with { StartHeld = true }, SessionStep.InGame, "a held start with no launch pending changes nothing");
        Is(Started with { SaveSyncPending = true, LaunchPending = true, StartHeld = true }, SessionStep.ReceivingSave, "the save comes before the start gate");
        Is(Started with { BuildCheckPending = true, LaunchPending = true, StartHeld = true }, SessionStep.CheckingBuild, "so does the build check");
        Is(Started with { SessionStarted = false, LaunchPending = true, StartHeld = true }, SessionStep.Lobby, "and a session that has not started");
        Is(Started with { Bridged = true, BridgeReady = true }, SessionStep.InGame, "bridged with the adapter linked: in game");
        Is(Started with { Bridged = true, BridgeWasReady = true }, SessionStep.GameClosed, "the adapter link was there and is gone: game closed");
        Is(Started with { Bridged = true }, SessionStep.StartingGame, "bridged and the adapter never linked: starting game");
        Is(Started, SessionStep.InGame, "no bridge (the game's own transport): in game once started");
    }

    private static void Gate()
    {
        Check.That(StartGate.Open(true, held: false, requested: false, SessionStep.None, latched: false), "a host with nothing held (command line, game running) is open");
        Check.That(!StartGate.Open(true, held: true, requested: false, SessionStep.None, latched: false), "a held host waits for START GAME");
        Check.That(StartGate.Open(true, held: true, requested: true, SessionStep.None, latched: false), "and starts once it asked");
        Check.That(!StartGate.Open(true, held: true, requested: false, SessionStep.InGame, latched: true), "the host's gate ignores what the host publishes");
        Check.That(StartGate.Open(false, held: false, requested: false, SessionStep.None, latched: false), "a guest with nothing held is open");
        Check.That(!StartGate.Open(false, held: true, requested: true, SessionStep.Ready, latched: false), "a held guest waits while the host is ready");
        Check.That(StartGate.Open(false, held: true, requested: false, SessionStep.StartingGame, latched: false), "a guest starts when the host's game is starting");
        Check.That(StartGate.Open(false, held: true, requested: false, SessionStep.InGame, latched: false), "or already running");
        Check.That(StartGate.Open(false, held: true, requested: false, SessionStep.Lobby, latched: true), "a latched guest stays open when the host blinks");
        foreach (var step in new[] { SessionStep.None, SessionStep.Lobby, SessionStep.CheckingBuild, SessionStep.ReceivingSave, SessionStep.GameClosed, SessionStep.Refused, SessionStep.Ready })
            Check.That(!StartGate.Open(false, held: true, requested: false, step, latched: false), $"a guest does not start on the host's {step}");
    }

    private static void Codec()
    {
        Check.That(StepCodec.Encode(SessionStep.ReceivingSave, 40, null) == "step=ReceivingSave;pct=40", "receiving save carries its percent");
        Check.That(StepCodec.Encode(SessionStep.Refused, 0, RefusalKind.ModFiles) == "step=Refused;why=ModFiles", "refused carries its kind");
        Check.That(StepCodec.Encode(SessionStep.InGame, 77, RefusalKind.ModFiles) == "step=InGame", "other steps carry neither");
        Check.That(StepCodec.Encode(SessionStep.ReceivingSave, 140, null) == "step=ReceivingSave;pct=100", "the percent is capped");

        foreach (var step in Enum.GetValues<SessionStep>())
        {
            var ok = StepCodec.TryDecode(StepCodec.Encode(step, 0, null), out var back, out _, out _);
            Check.That(ok && back == step, $"{step} survives a round trip");
        }
        Check.That(StepCodec.TryDecode("step=ReceivingSave;pct=40", out var save, out var percent, out var none)
                   && save == SessionStep.ReceivingSave && percent == 40 && none == null, "save step and percent decode");
        Check.That(StepCodec.TryDecode("step=Refused;why=ModFiles", out var refused, out _, out var why)
                   && refused == SessionStep.Refused && why == RefusalKind.ModFiles, "refused step and kind decode");
        Check.That(!StepCodec.TryDecode("", out _, out _, out _), "empty text does not decode");
        Check.That(!StepCodec.TryDecode("searching 3", out _, out _, out _), "foreign text does not decode");
        Check.That(!StepCodec.TryDecode("step=Dancing", out _, out _, out _), "an unknown step does not decode");
        Check.That(!StepCodec.TryDecode("step=5", out _, out _, out _), "a numeric step does not decode");
        Check.That(StepCodec.TryDecode("step=Refused;why=Nonsense", out _, out _, out var unknownWhy) && unknownWhy == null, "an unknown kind is dropped, the step stays");
    }

    private static void Meter()
    {
        var meter = new SaveProgressMeter();
        Check.That(meter.Percent == 0 && meter.CurrentFile == "" && meter.ReceivedBytes == 0 && meter.TotalBytes == 0, "0 before the first FILE_BEGIN, no file");
        meter.OnChunk(1, 50);
        Check.That(meter.Percent == 0, "a chunk of a file never announced counts for nothing");
        meter.OnBegin(1, "a", 100);
        meter.OnBegin(2, "b", 300);
        meter.OnChunk(1, 100);
        Check.That(meter.Percent == 25, "bytes over the sum of the announced sizes");
        Check.That(meter.CurrentFile == "a" && meter.ReceivedBytes == 100 && meter.TotalBytes == 400, "the file, bytes in and bytes announced");
        meter.OnChunk(2, 150);
        Check.That(meter.Percent == 62, "a second file adds to both");
        Check.That(meter.CurrentFile == "b" && meter.ReceivedBytes == 250, "the current file is the one that last received a chunk");
        meter.OnChunk(2, 5000);
        Check.That(meter.Percent == 100, "capped at 100");
        meter.OnBegin(1, "a", 100);
        Check.That(meter.Percent == 100, "a file announced again restarts only itself");
        meter.OnChunk(1, 100);
        Check.That(meter.Percent == 100, "and completes again");

        var resent = new SaveProgressMeter();
        resent.OnBegin(1, "a", 100);
        resent.OnChunk(1, 50);
        resent.OnBegin(2, "a", 100);
        Check.That(resent.Percent == 0, "a file sent again under a new transfer id replaces its earlier transfer");
        resent.OnChunk(2, 100);
        Check.That(resent.Percent == 100, "so the resent set still reaches 100");
        Check.That(resent.CurrentFile == "a" && resent.TotalBytes == 100 && resent.ReceivedBytes == 100, "and counts the resent file once");

        var restarted = new SaveProgressMeter();
        restarted.OnBegin(1, "a", 100);
        restarted.OnChunk(1, 10);
        restarted.OnBegin(2, "a", 100);
        Check.That(restarted.CurrentFile == "", "a file replaced before its new chunk is not the current one");
    }

    private static void Is(StepInputs inputs, SessionStep expected, string what) =>
        Check.That(SessionProgress.Step(inputs) == expected, what);
}
