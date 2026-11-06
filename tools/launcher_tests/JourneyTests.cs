using CoopLauncher;
using CoopLauncher.Gui;

namespace LauncherTests;

/// <summary>The player journey of a game where the guest brings its character (Dragon's Dogma 2): the words of a card and a screen
/// for connecting, picking, reading and the game's own state, what a mod that did not load looks like in the logs, the capture's
/// answers and the ini a capture leaves.</summary>
public static class JourneyTests
{
    private const ulong LobbyId = 109775243712345678;
    private const long Now = 100_000;

    private static readonly GameProfile Dd2 = GameProfile.Load("dd2");
    private static readonly GameStatus Mod = new(new ModStatus(ModState.Installed, 412), true, 412, 26256634);

    public static void Run()
    {
        using var data = new TempData();
        TempData.UseDefaults();
        Cards();
        Screens();
        Capture();
        ModLoad();
        IniReset();
        RefusedJoin();
    }

    private static void RefusedJoin()
    {
        Check.That(SteamLobby.ParseRefused("LauncherBuild:412") is (RefusalKind.LauncherBuild, "412") && SteamLobby.ParseRefused("") == null && SteamLobby.ParseRefused("7:1") == null
                   && SteamLobby.ParseRefused("Nonsense:1") == null, "a launcher that could not join publishes its kind and build");
    }

    private static PlayerSlot Slot(SessionStep step, bool host = false, bool local = false, string detail = "", string label = "", GameState? game = null,
        bool connecting = false, RefusalKind? refusal = null, string name = "Amy") =>
        new(host ? 2UL : 1UL, host ? "Precyse" : name, host, local, Step: step, RttMs: local ? null : 20, Refusal: refusal, Connecting: connecting, Detail: detail, Label: label, Game: game);

    private static void Cards()
    {
        Check.That(CardStates.Words(Slot(SessionStep.Lobby, connecting: true)) == "Connecting" && CardStates.Of(Slot(SessionStep.Lobby, connecting: true)) == CardState.Progress,
            "card: a peer never heard from is connecting, not silent");
        Check.That(CardStates.Words(Slot(SessionStep.PickingCharacter)) == "Picking character" && CardStates.Of(Slot(SessionStep.PickingCharacter)) == CardState.Idle, "card: picking a character");
        Check.That(CardStates.Words(Slot(SessionStep.CharacterNotRead, detail: "No save")) == "Character not read: No save" && CardStates.Of(Slot(SessionStep.CharacterNotRead)) == CardState.Problem,
            "card: a character that could not be read says why");
        Check.That(CardStates.IsPrepared(Slot(SessionStep.Ready)) && CardStates.IsPrepared(Slot(SessionStep.InGame)) && !CardStates.IsPrepared(Slot(SessionStep.PickingCharacter)),
            "card: prepared means ready or further");
        Check.That(CardStates.Words(Slot(SessionStep.Refused, refusal: RefusalKind.SaveResignFailed, detail: "data000.bin")) == "Amy could not use the save", "card: the host's card for a save that could not be converted");
        Check.That(CardStates.Words(Slot(SessionStep.Refused, local: true, refusal: RefusalKind.SaveResignFailed, detail: "data000.bin")) == "data000.bin could not be converted to your Steam account",
            "card: the player's own card says what happened");

        var host = Slot(SessionStep.InGame, host: true, game: new GameState(GamePlace.World, GameCharacter.Off, GameSave.Ok, 3));
        PlayerSlot Guest(GameState state) => Slot(SessionStep.InGame, game: state);
        Check.That(CardStates.Words(Guest(new GameState(GamePlace.Title)), host) == "Load slot 3 like the host" && CardStates.Of(Guest(new GameState(GamePlace.Title)), host) == CardState.Problem,
            "card: a guest at its title is told the host's slot");
        Check.That(CardStates.Words(Guest(new GameState(GamePlace.World, GameCharacter.Ok, GameSave.Ok, 5)), host) == "Different save than the host", "card: another slot than the host's");
        Check.That(CardStates.Words(Guest(new GameState(GamePlace.World, GameCharacter.Ok, GameSave.Ok, 3)), host) == "In world" && CardStates.Of(Guest(new GameState(GamePlace.World, GameCharacter.Ok, GameSave.Ok, 3)), host) == CardState.InGame,
            "card: the same slot in the world");
        Check.That(CardStates.Words(Guest(new GameState(GamePlace.World, GameCharacter.None, GameSave.Ok, 3)), host) == "Character not loaded", "card: a character that is not loaded");
        Check.That(CardStates.Words(Guest(new GameState(GamePlace.Loading, GameCharacter.Ok, GameSave.Refused)), host) == "Save not protected", "card: a save that is not protected");
        Check.That(CardStates.Words(Guest(new GameState(GamePlace.Loading, GameCharacter.Ok, GameSave.Ok, 3)), host) == "Loading", "card: loading");
        Check.That(CardStates.Words(Slot(SessionStep.InGame, host: true, game: new GameState(GamePlace.Title, GameCharacter.Off, GameSave.Off)), host) == "At title", "card: the host at its title");
        Check.That(CardStates.Words(Slot(SessionStep.InGame)) == "In game", "card: in game without a report");
    }

    private static AppStatus Status(SessionStep step, params PlayerSlot[] players) =>
        new(AppState.Joined, LobbyId) { GameId = "dd2", Step = step, Seats = 3, Bridged = true, Players = players };

    private static void Screens()
    {
        var host = Slot(SessionStep.Lobby, host: true, label: "Fighting Arisen");
        var local = (SessionStep step) => Slot(step, local: true);
        string Texts(SessionModel model) => string.Join("/", model.Steps.Select(row => row.Text));

        var picking = SessionScreens.Session(Status(SessionStep.PickingCharacter, host, local(SessionStep.PickingCharacter)), Dd2, null, Mod, GameCondition.Fine, [], Now);
        Check.That(Texts(picking) == "Lobby/World: Fighting Arisen/Build check/Receiving save/Choose character/Waiting for Precyse/Starting game/In game",
            "guest: the steps show the host's world and the character to choose before waiting for the host");
        Check.That(picking.StartEnabled && picking.Primary == "READY", "guest: READY is usable while it picks");
        var rows = picking.Steps;
        Check.That(rows[4].Mark == StepMark.Active && rows[1].Mark == StepMark.Done, "guest: choosing is the active step");

        var reading = SessionScreens.Session(Status(SessionStep.ReadingCharacter, host, local(SessionStep.ReadingCharacter)) with { Capture = new CaptureView(CaptureState.Running) }, Dd2, null, Mod, GameCondition.Fine, [], Now);
        Check.That(reading.Steps[4].Text == "Reading your character" && reading.Steps[4].Lines![0] == "Dragon's Dogma 2 opens to its title and closes by itself" && !reading.StartEnabled && reading.Primary == "READING CHARACTER",
            "guest: reading says the game opens to its title and closes by itself");

        var failed = SessionScreens.Session(Status(SessionStep.CharacterNotRead, host, local(SessionStep.CharacterNotRead)) with { Capture = new CaptureView(CaptureState.Failed, "That save has no character yet", @"C:\g\coop\adapter.log") },
            Dd2, null, Mod, GameCondition.Fine, [], Now);
        Check.That(failed.Steps[4] is { Mark: StepMark.Failed, Text: "Character not read" } && failed.Steps[4].Lines!.SequenceEqual(["That save has no character yet", @"C:\g\coop\adapter.log"]) && failed.Primary == "READY" && failed.StartEnabled,
            "guest: a character not read fails its row with the reason and the log path, and READY tries again");

        var waiting = SessionScreens.Session(Status(SessionStep.Ready, host, local(SessionStep.Ready)), Dd2, null, Mod, GameCondition.Fine, [], Now);
        Check.That(waiting.Primary == "WAITING FOR HOST" && !waiting.StartEnabled && waiting.Steps[4].Text == "Character chosen", "guest: waiting for the host only once ready");
        Check.That(SessionScreens.Session(Status(SessionStep.ReceivingSave, host, local(SessionStep.ReceivingSave)), Dd2, null, Mod, GameCondition.Fine, [], Now).Primary == "RECEIVING SAVE", "guest: the button names the save");
        Check.That(SessionScreens.Session(Status(SessionStep.CheckingBuild, host, local(SessionStep.CheckingBuild)) with { ConnectingToHost = true }, Dd2, null, Mod, GameCondition.Fine, [], Now) is { Primary: "CONNECTING" } connecting
                   && connecting.Steps[2].Text == "Connecting to Precyse", "guest: connecting to the host until its first frame");

        var hostLocal = Slot(SessionStep.Ready, host: true, local: true);
        Check.That(!SessionScreens.Session(Status(SessionStep.Ready, hostLocal, Slot(SessionStep.PickingCharacter)), Dd2, null, Mod, GameCondition.Fine, [], Now).StartEnabled, "host: START waits for a guest still picking");
        Check.That(SessionScreens.Session(Status(SessionStep.Ready, hostLocal, Slot(SessionStep.Ready)), Dd2, null, Mod, GameCondition.Fine, [], Now).StartEnabled, "host: START opens once every guest is ready");
        var cards = SessionScreens.Session(Status(SessionStep.Ready, hostLocal with { Label = "Fighting Arisen" }, Slot(SessionStep.Ready, label: "Amy / Lv 7")), Dd2, null, Mod, GameCondition.Fine, [], Now).Seats;
        Check.That(cards[0].Detail == "Fighting Arisen" && cards[1].Detail == "Amy / Lv 7", "cards: the detail line shows the world and the character");

        var refusal = new Refusal(RefusalKind.ModNotLoaded, GameId: "dd2");
        var modFailed = SessionScreens.Joining(Status(SessionStep.Refused, host, local(SessionStep.Refused)) with { Refusal = refusal }, Dd2, Mod, null, GameCondition.Fine, 0, Now, @"C:\g");
        Check.That(modFailed.Steps.Single(row => row.Mark == StepMark.Failed) is { Text: "The co-op mod did not load" } row && row.Lines![0].EndsWith("adapter.log", StringComparison.Ordinal),
            "joining: a mod that did not load fails the starting row with the log path");

        Check.That(OptionsModel.SessionPickers(Dd2, null, SessionStep.Ready, isHost: true).Count == 1 && OptionsModel.SessionPickers(Dd2, null, SessionStep.Ready, isHost: true, worldCommitted: true).Count == 0,
            "host: the campaign picker is locked once a guest reads its character");
        Check.That(OptionsModel.SessionPickers(Dd2, null, SessionStep.Ready, isHost: false, worldCommitted: true).Count == 1, "guest: its own picker stays");
        Check.That(SessionProgress.WorldCommitted(SessionStep.ReadingCharacter) && SessionProgress.WorldCommitted(SessionStep.Ready) && !SessionProgress.WorldCommitted(SessionStep.PickingCharacter),
            "a guest has committed the world from reading its character on");
    }

    private static void Capture()
    {
        Check.That(CharacterCapture.ViewOf("failed: empty: nothing", "Dragon's Dogma 2").Reason == "That save has no character yet: load it in Dragon's Dogma 2, save once, then press READY", "capture: empty");
        Check.That(CharacterCapture.ViewOf("failed: title: x", "Dragon's Dogma 2").Reason.EndsWith("press READY to try again", StringComparison.Ordinal), "capture: title ends with try again");
        foreach (var code in new[] { "load", "redirect", "write", "timeout" })
            Check.That(CharacterCapture.ViewOf($"failed: {code}: x", "G").Reason.EndsWith("press READY to try again", StringComparison.Ordinal), $"capture: {code} ends with try again");
        Check.That(CharacterCapture.ViewOf("failed: weird: it broke", "G").Reason == "it broke" && CharacterCapture.ViewOf("failed: it broke", "G").Reason == "it broke", "capture: an unknown code gives the detail");
        Check.That(CharacterCapture.ViewOf("ok").State == CaptureState.Done, "capture: ok");
        Check.That(CharacterCapture.ViewOf("nonsense").State == CaptureState.Failed, "capture: unreadable");
    }

    private static void ModLoad()
    {
        var dir = Path.Combine(Path.GetTempPath(), "cf_modload_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(Path.Combine(dir, "coop"));
        try
        {
            var started = DateTime.UtcNow;
            var profile = Dd2 with { AdapterLog = "coop/adapter.log", FrameworkLog = "re2_framework_log.txt" };
            Check.That(ModLoadCheck.Diagnose(profile, dir, started) is { Kind: RefusalKind.FrameworkNotLoaded }, "mod load: no loader log, the loader did not load");
            File.WriteAllText(Path.Combine(dir, "re2_framework_log.txt"), "loaded");
            Check.That(ModLoadCheck.Diagnose(profile, dir, started) is { Kind: RefusalKind.ModNotLoaded }, "mod load: a loader log but no mod log, the mod did not load");
            File.SetLastWriteTimeUtc(Path.Combine(dir, "re2_framework_log.txt"), started.AddMinutes(-5));
            Check.That(ModLoadCheck.Diagnose(profile, dir, started) is { Kind: RefusalKind.FrameworkNotLoaded }, "mod load: a loader log older than the start is no proof");
            File.SetLastWriteTimeUtc(Path.Combine(dir, "re2_framework_log.txt"), started.AddSeconds(1));

            var stamp = started.ToLocalTime().AddSeconds(1).ToString("yyyy-MM-dd HH:mm:ss.fff");
            File.WriteAllText(Path.Combine(dir, "coop", "adapter.log"), $"{stamp} dd2coop: start\n{stamp} build_gate: DD2.exe stamp 0x1, supported 0x2: co-op off\n");
            Check.That(ModLoadCheck.Diagnose(profile, dir, started) is { Kind: RefusalKind.GameUnsupported }, "mod load: a mod that switched itself off for this build");
            File.WriteAllText(Path.Combine(dir, "coop", "adapter.log"), $"{started.ToLocalTime().AddHours(-1):yyyy-MM-dd HH:mm:ss.fff} build_gate: stamp 0x1 co-op off\n{stamp} dd2coop: start\n");
            Check.That(ModLoadCheck.Diagnose(profile, dir, started) == null, "mod load: an earlier run's line is not this run's");
            ModLoadLines(profile, dir, started, stamp);
        }
        finally
        {
            Directory.Delete(dir, recursive: true);
        }
    }

    /// <summary>What the adapter's own lines make of a started game: the build gate of CD (stamp=0x) and DD2, a named other co-op mod, any
    /// other "co-op off" reason, and an adapter waiting for the title screen.</summary>
    private static void ModLoadLines(GameProfile profile, string dir, DateTime started, string stamp)
    {
        var log = Path.Combine(dir, "coop", "adapter.log");
        void Write(params string[] lines) => File.WriteAllLines(log, lines.Select(line => $"{stamp} {line}"));
        Write("adapter: start (Crimson Desert)", "build_gate: exe stamp=0x6AEB7D5A base=0x140000000 loaded at 0x140000000: build_mismatch, co-op off");
        Check.That(ModLoadCheck.Diagnose(profile, dir, started) is { Kind: RefusalKind.GameUnsupported }, "mod load: the CD build gate line is an unsupported build");
        Write("adapter: start", "build_gate: conflict: cdmp present: co-op off");
        Check.That(ModLoadCheck.Diagnose(profile, dir, started) is { Kind: RefusalKind.OtherCoopMod, Detail: "cdmp" }, "mod load: a conflict line names the other co-op mod");
        Write("build_gate: conflict: DD2gether present (reframework/autorun/coop): co-op off");
        Check.That(ModLoadCheck.Diagnose(profile, dir, started) is { Kind: RefusalKind.OtherCoopMod, Detail: "DD2gether" }, "mod load: DD2's conflict line names its mod too");
        Write("adapter: start", "init: no save folder: co-op off: the save folder is missing");
        var inert = ModLoadCheck.Diagnose(profile, dir, started);
        Check.That(inert is { Kind: RefusalKind.ModNotStarted, Detail: "the save folder is missing" }
            && StatusText.Refusal(inert) == "The co-op mod could not start: the save folder is missing", "mod load: any other co-op off line gives its reason");
        Write("adapter: start", "init: something went wrong, co-op off");
        Check.That(StatusText.Refusal(ModLoadCheck.Diagnose(profile, dir, started)!) == "The co-op mod could not start", "mod load: a co-op off line without a reason");
        Check.That(StatusText.Refusal(new Refusal(RefusalKind.OtherCoopMod, "cdmp")) == "Another co-op mod (cdmp) is installed", "mod load: the words for another co-op mod");

        Write("adapter: start (Crimson Desert)", "build_gate: exe stamp=0x6AEB7D59 base=0x140000000 loaded at 0x140000000: supported", "adapter: waiting for the title (WorldSystem)");
        Check.That(ModLoadCheck.Diagnose(profile, dir, started) == null && ModLoadCheck.WaitingForTitle(profile, dir, started), "mod load: an adapter waiting for the title is no failure but a wait");
        Write("adapter: waiting for the title (WorldSystem)", "adapter: WorldSystem present");
        Check.That(!ModLoadCheck.WaitingForTitle(profile, dir, started), "mod load: no longer waiting once the title is there");
        File.WriteAllText(log, $"{started.ToLocalTime().AddHours(-1):yyyy-MM-dd HH:mm:ss.fff} adapter: waiting for the title (WorldSystem)\n");
        File.SetLastWriteTimeUtc(log, started.AddSeconds(1));
        Check.That(!ModLoadCheck.WaitingForTitle(profile, dir, started), "mod load: an earlier run's waiting line is not this run's");
    }

    private static void IniReset()
    {
        var dir = Path.Combine(Path.GetTempPath(), "cf_ini_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(Path.Combine(dir, "coop"));
        try
        {
            var ini = Path.Combine(dir, Dd2.AdapterIni);
            File.WriteAllLines(ini, ["coop=0", "capture=4", "arisen=x"]);
            AdapterSettings.Reset(dir, Dd2, Dd2.SaveSync!);
            Check.That(File.ReadAllLines(ini).Contains("capture=-1") && File.ReadAllLines(ini).Contains("arisen=x"), "reset: a capture left in the ini is taken out");
            var before = File.ReadAllText(ini);
            AdapterSettings.Reset(dir, Dd2, Dd2.SaveSync!);
            Check.That(File.ReadAllText(ini) == before, "reset: nothing to take out leaves the ini alone");
        }
        finally
        {
            Directory.Delete(dir, recursive: true);
        }
    }
}
