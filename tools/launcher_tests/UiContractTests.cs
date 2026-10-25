using CoopLauncher;
using CoopLauncher.Gui;
using CoopLauncher.Web;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.RegularExpressions;

namespace LauncherTests;

/// <summary>The two sides of the bridge agree: the C# command table equals the page's <c>COMMANDS</c>, and every fixture's state has the
/// shape of a snapshot of the same screen (compared wherever both sides hold a value). Skips what is not in the repo yet.</summary>
public static class UiContractTests
{
    private const string BridgePath = "launcher/web/js/bridge.js";
    private const string FixturesPath = "launcher/web/dev/fixtures";
    private const string ExportPrefix = "export default";

    public static void Run()
    {
        ShapeRules();
        var shapes = Shapes();
        string[] screens = ["home", "lobbies", "joining", "hosting", "lobby", "select", "settings", "mini"];
        Check.That(screens.All(shapes.ContainsKey), "contract: a snapshot shape exists for every screen and the mini page");
        Check.That(Differences(JsonNode.Parse(shapes["lobby"].ToJsonString()), shapes["lobby"], "state").Count == 0, "contract: a snapshot has its own shape");
        CommandTable();
        Fixtures(shapes);
    }

    // ---------------------------------------------------------------- command table

    private static void CommandTable()
    {
        var path = Path.Combine(RepoPaths.RepoRoot, BridgePath);
        if (!File.Exists(path))
        {
            Console.WriteLine("UiContractTests: skipped: no bridge.js");
            return;
        }
        var names = CommandsOf(File.ReadAllText(path));
        Check.That(names != null, "contract: bridge.js lists COMMANDS");
        if (names == null) return;
        var page = names.ToHashSet();
        var host = UiCommands.Types.ToHashSet();
        Check.That(page.SetEquals(host), $"contract: COMMANDS equals the C# table (page only: {string.Join(",", page.Except(host))}; C# only: {string.Join(",", host.Except(page))})");
        Check.That(names.Count == page.Count, "contract: COMMANDS lists no command twice");
    }

    /// <summary>The quoted names in the array that <c>COMMANDS</c> is set to, or null when there is no such array.</summary>
    public static List<string>? CommandsOf(string source)
    {
        var match = Regex.Match(source, @"COMMANDS\s*=\s*(?:Object\.freeze\(\s*)?\[(?<list>[^\]]*)\]", RegexOptions.Singleline);
        if (!match.Success) return null;
        return Regex.Matches(match.Groups["list"].Value, @"['""`](?<name>[^'""`]+)['""`]").Select(name => name.Groups["name"].Value).ToList();
    }

    // ---------------------------------------------------------------- fixtures

    private static void Fixtures(Dictionary<string, JsonNode> shapes)
    {
        var folder = Path.Combine(RepoPaths.RepoRoot, FixturesPath);
        var files = Directory.Exists(folder) ? Directory.GetFiles(folder, "*.js").Order().ToList() : [];
        if (files.Count == 0)
        {
            Console.WriteLine("UiContractTests: skipped: no fixtures");
            return;
        }
        foreach (var file in files)
        {
            var key = Path.GetFileNameWithoutExtension(file);
            JsonObject? fixture;
            try
            {
                fixture = JsonNode.Parse(StripExport(File.ReadAllText(file)))?.AsObject();
            }
            catch (JsonException e)
            {
                Check.That(false, $"contract: fixture {key} is not `export default <JSON>;` ({e.Message})");
                continue;
            }
            CheckFixture(key, fixture!, shapes);
        }
    }

    private static void CheckFixture(string key, JsonObject fixture, Dictionary<string, JsonNode> shapes)
    {
        if (fixture["state"] is JsonObject state)
        {
            var screen = state["screen"]?.GetValue<string>();
            if (screen == null || !shapes.TryGetValue(screen, out var shape)) Check.That(false, $"contract: fixture {key} has a state with a known screen (got {screen})");
            else foreach (var problem in Differences(state, shape, "state")) Check.That(false, $"contract: fixture {key}: {problem}");
        }
        if (fixture["mini"] is JsonObject mini)
            foreach (var problem in Differences(mini, shapes["mini"], "mini")) Check.That(false, $"contract: fixture {key}: {problem}");
    }

    /// <summary>`export default {...};` as the JSON inside.</summary>
    public static string StripExport(string text)
    {
        var trimmed = text.Trim();
        if (!trimmed.StartsWith(ExportPrefix, StringComparison.Ordinal)) throw new JsonException("no export default");
        return trimmed[ExportPrefix.Length..].Trim().TrimEnd(';');
    }

    // ---------------------------------------------------------------- shapes

    /// <summary>Compares a fixture's JSON with the shape of a C# snapshot. Keys the fixture omits are fine where the C# value is null;
    /// anywhere both sides are objects (or both arrays with an element) the keys must be the same and the values compared in turn;
    /// the fixture's own <c>type</c> and <c>seq</c> are not part of a snapshot.</summary>
    public static List<string> Differences(JsonNode? fixture, JsonNode? shape, string path)
    {
        var problems = new List<string>();
        if (fixture == null || shape == null) return problems;
        if (fixture is JsonObject fixtureObject && shape is JsonObject shapeObject)
        {
            foreach (var (name, value) in fixtureObject)
            {
                if (path == "state" && name is "type" or "seq" || value == null) continue;
                if (!shapeObject.ContainsKey(name)) problems.Add($"{path}.{name} is not in the snapshot");
                else problems.AddRange(Differences(value, shapeObject[name], $"{path}.{name}"));
            }
            foreach (var (name, value) in shapeObject)
                if (value != null && !fixtureObject.ContainsKey(name)) problems.Add($"{path}.{name} is missing");
            return problems;
        }
        if (fixture is JsonArray fixtureArray && shape is JsonArray shapeArray)
        {
            if (fixtureArray.Count > 0 && shapeArray.Count > 0) problems.AddRange(Differences(fixtureArray[0], shapeArray[0], path + "[]"));
            return problems;
        }
        if (fixture.GetValueKind() == JsonValueKind.Object != (shape.GetValueKind() == JsonValueKind.Object) || (fixture is JsonArray) != (shape is JsonArray))
            problems.Add($"{path} is {fixture.GetValueKind()} but the snapshot has {shape.GetValueKind()}");
        return problems;
    }

    private static void ShapeRules()
    {
        var shape = JsonNode.Parse("""{"a":{"b":1,"c":null},"list":[{"x":1}],"gone":null,"text":"t"}""");
        Check.That(Differences(JsonNode.Parse("""{"a":{"b":2,"c":{"free":1}},"list":[{"x":5}],"text":"u"}"""), shape, "state").Count == 0, "shape: the same keys match, a null in the snapshot accepts anything");
        Check.That(Differences(JsonNode.Parse("""{"a":{"b":2},"list":[{"x":5}],"text":"u","type":"state","seq":1}"""), shape, "state").Count == 0, "shape: type and seq of a state are ignored, a null fixture value or an omitted null key is fine");
        Check.That(Differences(JsonNode.Parse("""{"a":{"b":2,"extra":1},"list":[],"text":"u"}"""), shape, "state").Any(p => p.Contains("a.extra")), "shape: a key the snapshot lacks is reported");
        Check.That(Differences(JsonNode.Parse("""{"a":{},"list":[],"text":"u"}"""), shape, "state").Any(p => p.Contains("a.b is missing")), "shape: a key the fixture lacks is reported");
        Check.That(Differences(JsonNode.Parse("""{"a":{"b":1},"list":[{"y":1}],"text":"u"}"""), shape, "state").Count == 2, "shape: array elements are compared by their first element");
        Check.That(Differences(JsonNode.Parse("""{"a":5,"list":[],"text":"u"}"""), shape, "state").Any(p => p.Contains("state.a is")), "shape: a value where the snapshot has an object is reported");
        Check.That(StripExport("export default {\"a\": 1};\n") == "{\"a\": 1}", "fixture: export default and the semicolon are stripped");
        Check.That(CommandsOf("export const COMMANDS = ['host', \"join.code\",\n 'ready'];")?.SequenceEqual(["host", "join.code", "ready"]) == true, "bridge: COMMANDS is read from its array");
        Check.That(CommandsOf("export const COMMANDS = Object.freeze(['host']);")?.SequenceEqual(["host"]) == true, "bridge: a frozen array is read too");
        Check.That(CommandsOf("const other = [];") == null, "bridge: no COMMANDS is reported");
    }

    /// <summary>One merged snapshot shape per screen, from snapshots of the states the screen takes (so that fields that are null in
    /// one state, like the lobby state of a hosting screen, are filled by another).</summary>
    private static Dictionary<string, JsonNode> Shapes()
    {
        var games = new[] { GameProfile.Load("re0"), GameProfile.Load("sr2") };
        var mod = new GameStatus(new ModStatus(ModState.Installed, 412), true, 412, 17178773);
        SnapshotInput Input(AppStatus status, Intent intent = Intent.None, JoinTarget? target = null, Tab tab = Tab.Play, ScreenKind? view = null,
            PopupKind popup = PopupKind.None, ulong popupLobby = 0, string? selected = null) =>
            new(status, games.Select(profile => new GameEntry(profile, mod, new ArtDto("h", "l", "c"))).ToList(), selected ?? status.GameId ?? "re0",
                view ?? Screens.For(status, intent, target, tab), view, tab, popup, popupLobby, LobbyFilter.All, null, target, GameCondition.Fine,
                new UpdateView("Update to 413", true, 413, false), ["Amy joined"], 412, null, 1000, 1_800_000_000, _ => null, _ => 1);

        const ulong lobbyId = 109775243712345678;
        PlayerSlot host(bool local = false, SessionStep step = SessionStep.InGame) => new(2, "Precyse", true, local, Step: step, RttMs: local ? null : 18);
        PlayerSlot guest(SessionStep step, bool local = false, bool held = false) => new(1, "Amy", false, local, Step: step, RttMs: local ? null : 42, Held: held, HoldSecondsLeft: 5);
        AppStatus session(string game, AppState state, SessionStep step, params PlayerSlot[] players) =>
            new(state, lobbyId) { GameId = game, Step = step, Seats = game == "re0" ? 2 : 4, SeatCharacters = game == "re0" ? ["Rebecca", "Billy"] : [], Bridged = true, Players = players, SessionStartedUnix = 1_799_999_000,
                HostOptions = new Dictionary<string, string> { ["transport"] = "steam", ["max_players"] = "4" } };
        var listing = new LobbyListing(lobbyId, "sr2", 2, "Precyse", 2, 4, 412, 412, 17178773, new Dictionary<string, string> { ["transport"] = "steam" },
            [new RosterEntry("Precyse", SessionStep.InGame), new RosterEntry("Amy", SessionStep.Lobby)], 1_799_999_000, 33);
        var friend = new FriendLobby(2, "Precyse", lobbyId, "re0", listing with { GameId = "re0" });
        var refusal = new Refusal(RefusalKind.ModFiles, RefusalVersions.Format(412, 409), "re0", lobbyId);
        var target = new JoinTarget(lobbyId, "Precyse", 2);

        var byScreen = new Dictionary<string, List<JsonNode>>();
        void Add(string screen, MainState state) => (byScreen.TryGetValue(screen, out var list) ? list : byScreen[screen] = []).Add(JsonNode.Parse(UiJson.Write(state))!);
        void AddInput(SnapshotInput input) { var state = UiSnapshot.MainPage(input, null); Add(UiJson.Write(state.Screen).Trim('"'), state); }

        AddInput(Input(AppStatus.Idle with { FriendLobbies = [friend], Refusal = new Refusal(RefusalKind.ModNotInstalled, "", "re0", 0) }, popup: PopupKind.Options));
        AddInput(Input(AppStatus.Idle with { Lobbies = [listing] }, tab: Tab.Lobbies, popup: PopupKind.Lobby, popupLobby: lobbyId));
        AddInput(Input(AppStatus.Idle with { Refusal = refusal }, Intent.Join, target));
        AddInput(Input(session("re0", AppState.Hosting, SessionStep.Lobby, host(local: true, step: SessionStep.Lobby)), Intent.Host));
        AddInput(Input(session("re0", AppState.Joined, SessionStep.ReceivingSave, host(step: SessionStep.Ready), guest(SessionStep.ReceivingSave, local: true)), Intent.Join, target));
        AddInput(Input(session("re0", AppState.PeerConnected, SessionStep.InGame, host(), guest(SessionStep.InGame, local: true))));
        AddInput(Input(session("sr2", AppState.PeerConnected, SessionStep.InGame, host(), guest(SessionStep.InGame, local: true))));
        AddInput(Input(session("re0", AppState.PeerConnected, SessionStep.Ready, host(step: SessionStep.Ready), guest(SessionStep.Ready, local: true)), view: ScreenKind.Select));
        AddInput(Input(AppStatus.Idle, view: ScreenKind.Settings));
        AddInput(Input(AppStatus.Idle, view: ScreenKind.Settings, popup: PopupKind.Update));
        var failedRun = new Dictionary<string, ModRunView> { ["re0"] = new(ModRunKind.Install, new ModProgress(ModStep.Failed, 37, 3, 50, "The file is in use")) };
        var copyingRun = new Dictionary<string, ModRunView> { ["re0"] = new(ModRunKind.Install, new ModProgress(ModStep.Copying, 40, 12, 50)) };
        AddInput(Input(AppStatus.Idle) with { ModRuns = failedRun });
        AddInput(Input(AppStatus.Idle, view: ScreenKind.Settings) with { ModRuns = copyingRun });

        var shapes = byScreen.ToDictionary(pair => pair.Key, pair => Merge(pair.Value)!);
        var miniStatus = session("re0", AppState.PeerConnected, SessionStep.InGame, host(local: true), guest(SessionStep.InGame, held: true));
        shapes["mini"] = Merge([JsonNode.Parse(UiJson.Write(UiSnapshot.MiniPage(Input(miniStatus))))!,
            JsonNode.Parse(UiJson.Write(UiSnapshot.MiniPage(Input(session("re0", AppState.PeerConnected, SessionStep.InGame, host(local: true), guest(SessionStep.InGame))))))!])!;
        return shapes;
    }

    /// <summary>The union of several JSON values of one shape: every key any has, the value of the first that has one.</summary>
    private static JsonNode? Merge(List<JsonNode> nodes)
    {
        var present = nodes.Where(node => node != null).ToList();
        if (present.Count == 0) return null;
        if (present.All(node => node is JsonObject))
        {
            var merged = new JsonObject();
            foreach (var name in present.SelectMany(node => ((JsonObject)node).Select(pair => pair.Key)).Distinct())
                merged[name] = Merge(present.Select(node => ((JsonObject)node)[name]).OfType<JsonNode>().Select(Clone).ToList());
            return merged;
        }
        if (present.All(node => node is JsonArray))
        {
            var items = present.SelectMany(node => ((JsonArray)node).OfType<JsonNode>()).Select(Clone).ToList();
            return items.Count == 0 ? new JsonArray() : new JsonArray(Merge(items));
        }
        return Clone(present[0]);
    }

    private static JsonNode Clone(JsonNode node) => JsonNode.Parse(node.ToJsonString())!;
}
