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
        Check.That(!StartGate.Open(false, held: true, requested: false, SessionStep.InGame, latched: true, guestConfirms: true),
            "a guest that picks its character waits for its READY even once the host is in");
        Check.That(!StartGate.Open(false, held: true, requested: true, SessionStep.Ready, latched: false, guestConfirms: true),
            "and, ready, still waits for the host to start");
        Check.That(StartGate.Open(false, held: true, requested: true, SessionStep.InGame, latched: false, guestConfirms: true),
            "and starts once both are so");
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
        Check.That(StepCodec.Encode(new StepReport(SessionStep.ReceivingSave, 40)) == "step=ReceivingSave;pct=40", "receiving save carries its percent");
        Check.That(StepCodec.Encode(new StepReport(SessionStep.Refused, Refusal: RefusalKind.ModFiles)) == "step=Refused;why=ModFiles", "refused carries its kind");
        Check.That(StepCodec.Encode(new StepReport(SessionStep.Refused, Refusal: RefusalKind.GameBuild, Detail: "5/3")) == "step=Refused;why=GameBuild;detail=5/3", "refused carries its detail");
        Check.That(StepCodec.Encode(new StepReport(SessionStep.InGame, 77, RefusalKind.ModFiles)) == "step=InGame", "other steps carry neither");
        Check.That(StepCodec.Encode(new StepReport(SessionStep.ReceivingSave, 140)) == "step=ReceivingSave;pct=100", "the percent is capped");
        Check.That(StepCodec.Encode(new StepReport(SessionStep.CharacterNotRead, Detail: "no save; try=again")) == "step=CharacterNotRead;why=no save, try:again",
            "a character that was not read carries its reason, the separators removed");

        foreach (var step in Enum.GetValues<SessionStep>())
        {
            var ok = StepCodec.TryDecode(StepCodec.Encode(new StepReport(step)), out var back);
            Check.That(ok && back.Step == step, $"{step} survives a round trip");
        }
        Check.That(StepCodec.TryDecode("step=ReceivingSave;pct=40", out var save) && save is { Step: SessionStep.ReceivingSave, Percent: 40, Refusal: null }, "save step and percent decode");
        Check.That(StepCodec.TryDecode("step=Refused;why=ModFiles", out var refused) && refused is { Step: SessionStep.Refused, Refusal: RefusalKind.ModFiles }, "refused step and kind decode");
        Check.That(StepCodec.TryDecode("step=Refused;why=GameBuild;detail=5/3", out var detailed) && detailed.Detail == "5/3", "the refusal detail decodes");
        Check.That(StepCodec.TryDecode("step=CharacterNotRead;why=no save", out var unread) && unread is { Step: SessionStep.CharacterNotRead, Detail: "no save", Refusal: null }, "the reason of an unread character decodes");
        Check.That(!StepCodec.TryDecode("", out _), "empty text does not decode");
        Check.That(!StepCodec.TryDecode("searching 3", out _), "foreign text does not decode");
        Check.That(!StepCodec.TryDecode("step=Dancing", out _), "an unknown step does not decode");
        Check.That(!StepCodec.TryDecode("step=5", out _), "a numeric step does not decode");
        Check.That(StepCodec.TryDecode("step=Refused;why=Nonsense", out var unknownWhy) && unknownWhy.Refusal == null, "an unknown kind is dropped, the step stays");

        var game = new GameState(GamePlace.World, GameCharacter.Ok, GameSave.Ok, 2);
        var text = StepCodec.Encode(new StepReport(SessionStep.InGame, Label: "Arisen / Lv 4", Game: game));
        Check.That(text == "step=InGame;label=Arisen / Lv 4;place=world;char=ok;save=ok;slot=2", "label and game state are published as pairs");
        Check.That(StepCodec.TryDecode(text, out var back2) && back2.Label == "Arisen / Lv 4" && back2.Game == game, "label and game state decode");
        Check.That(StepCodec.TryDecode("step=InGame", out var plain) && plain.Game == null && plain.Label == "", "a step without game state has none");
        Check.That(GameState.Parse("place=title;char=off;save=refused;slot=-1;peers=1:0"u8.ToArray()) == new GameState(GamePlace.Title, GameCharacter.Off, GameSave.Refused),
            "GAME_STATE parses its pairs, an unknown key is skipped and slot -1 is unknown");
        Check.That(GameState.Parse("place=nowhere;char=7"u8.ToArray()).IsEmpty, "an unknown value stays unknown");
        var cd = GameState.Parse("place=world;char=off;save=off;slot=-1;copy=refused:silent;peers=76561198000000001:0,5:2"u8.ToArray());
        Check.That(cd == new GameState(GamePlace.World, GameCharacter.Off, GameSave.Off, GameState.UnknownSlot, GameCopy.Refused, "silent", true),
            "GAME_STATE parses the copy with its code and a peer in state 2 as a mod that differs");
        Check.That(GameState.Parse("copy=ok;peers=7:0,8:1"u8.ToArray()) == new GameState(Copy: GameCopy.Ok), "a live or stale peer is not a difference");
        Check.That(GameState.Parse("copy=off"u8.ToArray()).Copy == GameCopy.Off && GameState.Parse("copy=maybe"u8.ToArray()).Copy == GameCopy.Unknown, "copy off parses and an unknown copy value stays unknown");
        var re0 = GameState.Parse("place=world;coop=0;load=failed"u8.ToArray());
        Check.That(re0 == new GameState(GamePlace.World, Coop: false, Load: GameLoad.Failed), "GAME_STATE parses coop and load");
        Check.That(GameState.Parse("coop=1;load=ok"u8.ToArray()) == new GameState(Coop: true, Load: GameLoad.Ok) && GameState.Parse("coop=x;load=7"u8.ToArray()).IsEmpty, "coop 1 and load ok parse, other values stay unknown");
        var re0Text = StepCodec.Encode(new StepReport(SessionStep.InGame, Game: re0));
        Check.That(re0Text == "step=InGame;place=world;coop=0;load=failed" && StepCodec.TryDecode(re0Text, out var re0Back) && re0Back.Game == re0, "coop and load are published as pairs and decode");
        var cdText = StepCodec.Encode(new StepReport(SessionStep.InGame, Game: cd));
        Check.That(cdText == "step=InGame;place=world;char=off;save=off;copy=refused:silent;differs=1", "copy and difference are published as pairs");
        Check.That(StepCodec.TryDecode(cdText, out var cdBack) && cdBack.Game == cd, "copy and difference decode from the published step");
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
