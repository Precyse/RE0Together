using CoopLauncher.Web;

namespace LauncherTests;

/// <summary>What the web window accepts from its page: every command valid and invalid, the origin, the image host's routes and the
/// rate-limited drop log.</summary>
public static class UiCommandsTests
{
    private static readonly Dictionary<string, string> Valid = new()
    {
        [Cmd.Ready] = """{"type":"ready"}""",
        [Cmd.PageRendered] = """{"type":"page.rendered","seq":7}""",
        [Cmd.WindowMinimize] = """{"type":"window.minimize"}""",
        [Cmd.WindowClose] = """{"type":"window.close"}""",
        [Cmd.CloseConfirm] = """{"type":"close.confirm","leave":true}""",
        [Cmd.TabSelect] = """{"type":"tab.select","tab":"lobbies"}""",
        [Cmd.GameSelect] = """{"type":"game.select","gameId":"re0"}""",
        [Cmd.Host] = """{"type":"host"}""",
        [Cmd.JoinCode] = """{"type":"join.code","code":"109775243712345678"}""",
        [Cmd.JoinFriend] = """{"type":"join.friend","lobbyId":"109775243712345678"}""",
        [Cmd.LobbyOpen] = """{"type":"lobby.open","lobbyId":"1"}""",
        [Cmd.LobbyAction] = """{"type":"lobby.action","lobbyId":"1"}""",
        [Cmd.LobbiesFilter] = """{"type":"lobbies.filter","filter":"friends"}""",
        [Cmd.LobbiesGame] = """{"type":"lobbies.game","gameId":null}""",
        [Cmd.PopupOptions] = """{"type":"popup.options"}""",
        [Cmd.PopupClose] = """{"type":"popup.close"}""",
        [Cmd.OptionSet] = """{"type":"option.set","gameId":"sr2","key":"transport","value":"steam"}""",
        [Cmd.ModInstall] = """{"type":"mod.install","gameId":"re0"}""",
        [Cmd.ModRetry] = """{"type":"mod.retry","gameId":"re0"}""",
        [Cmd.NoticeFix] = """{"type":"notice.fix"}""",
        [Cmd.ReportCreate] = """{"type":"report.create"}""",
        [Cmd.SessionCancel] = """{"type":"session.cancel"}""",
        [Cmd.SessionStart] = """{"type":"session.start"}""",
        [Cmd.SessionLeave] = """{"type":"session.leave"}""",
        [Cmd.SessionCopyCode] = """{"type":"session.copyCode"}""",
        [Cmd.SessionRelaunch] = """{"type":"session.relaunch"}""",
        [Cmd.SessionMini] = """{"type":"session.mini"}""",
        [Cmd.SessionPlaySolo] = """{"type":"session.playSolo"}""",
        [Cmd.MiniExpand] = """{"type":"mini.expand"}""",
        [Cmd.MiniClosed] = """{"type":"mini.closed"}""",
        [Cmd.ViewOpen] = """{"type":"view.open","view":"select","character":"Rebecca"}""",
        [Cmd.ViewClose] = """{"type":"view.close"}""",
        [Cmd.SelectStep] = """{"type":"select.step","half":1,"direction":-1}""",
        [Cmd.SelectSwap] = """{"type":"select.swap"}""",
        [Cmd.SettingsUpdates] = """{"type":"settings.updates","on":false}""",
        [Cmd.SettingsVisibility] = """{"type":"settings.visibility","value":"everyone"}""",
        [Cmd.SettingsLogs] = """{"type":"settings.logs"}""",
        [Cmd.GameBrowse] = """{"type":"game.browse","gameId":"ds2"}""",
        [Cmd.GameResetFolder] = """{"type":"game.resetFolder","gameId":"ds2"}""",
        [Cmd.GameUninstall] = """{"type":"game.uninstall","gameId":"ds2"}""",
        [Cmd.GameArchive] = """{"type":"game.archive","gameId":"ds2"}""",
        [Cmd.UpdateClick] = """{"type":"update.click"}""",
    };

    private static readonly (string Json, string Why)[] Invalid =
    [
        ("""{"type":"nope"}""", "an unknown type"),
        ("""{"type":"host","extra":1}""", "an extra field"),
        ("""{"type":"game.select"}""", "a missing field"),
        ("""{"type":"game.select","gameId":""}""", "an empty id"),
        ("""{"type":"game.select","gameId":5}""", "a number for text"),
        ("""{"type":"game.select","gameId":null}""", "null for a required text"),
        ("""{"type":"tab.select","tab":"settings"}""", "a value outside the enum"),
        ("""{"type":"view.open","view":"home"}""", "a view outside the enum"),
        ("""{"type":"close.confirm","leave":"yes"}""", "text for a flag"),
        ("""{"type":"join.friend","lobbyId":"12a"}""", "a lobby id that is not digits"),
        ("""{"type":"join.friend","lobbyId":123}""", "a lobby id as a number"),
        ("""{"type":"join.friend","lobbyId":"123456789012345678901"}""", "a lobby id of 21 digits"),
        ("""{"type":"join.friend","lobbyId":"99999999999999999999"}""", "a lobby id beyond ulong"),
        ("""{"type":"select.step","half":2,"direction":1}""", "a half out of range"),
        ("""{"type":"select.step","half":0,"direction":0}""", "a direction of zero"),
        ("""{"type":"select.step","half":0.5,"direction":1}""", "a fractional half"),
        ("""{"type":"page.rendered","seq":-1}""", "a negative sequence"),
        ("""{"type":"lobbies.game","gameId":5}""", "a number for the nullable game"),
        ("""{"type":["host"]}""", "a type that is not text"),
        ("""{"gameId":"re0"}""", "no type"),
        ("""["host"]""", "an array"),
        ("""42""", "a number"),
        ("{\"type\":\"host\"", "broken JSON"),
        ("""{"type":"game.select","gameId":{"a":{"b":{"c":1}}}}""", "nesting deeper than 3"),
        ("{\"type\":\"join.code\",\"code\":\"" + new string('1', UiCommands.MaxText + 1) + "\"}", "text over 64 characters"),
        ("{\"type\":\"" + new string('a', UiCommands.MaxBytes) + "\"}", "a message of 4 KB"),
    ];

    public static void Run()
    {
        EveryCommandValid();
        InvalidMessages();
        ParsedValues();
        Origin();
        DropLogLimit();
        ImageRoutesAreStrict();
    }

    private static void EveryCommandValid()
    {
        Check.That(Valid.Keys.OrderBy(name => name).SequenceEqual(UiCommands.Types.OrderBy(name => name)), "commands: a valid sample exists for every type of the table, and no other");
        foreach (var (type, json) in Valid)
        {
            var ok = UiCommands.Parse(json, out var command, out var error, out var claimed);
            Check.That(ok && command?.Type == type && claimed == type, $"commands: {type} parses ({error})");
        }
    }

    private static void InvalidMessages()
    {
        foreach (var (json, why) in Invalid)
        {
            var ok = UiCommands.Parse(json, out var command, out var error, out _);
            Check.That(!ok && command == null && error.Length > 0, $"commands: {why} is dropped");
        }
    }

    private static void ParsedValues()
    {
        UiCommands.Parse(Valid[Cmd.OptionSet], out var option, out _, out _);
        Check.That(option!.Text("gameId") == "sr2" && option.Text("key") == "transport" && option.Text("value") == "steam", "commands: option.set keeps its three texts");
        UiCommands.Parse(Valid[Cmd.JoinFriend], out var friend, out _, out _);
        Check.That(friend!.LobbyId() == 109775243712345678UL, "commands: a lobby id is read from its digits");
        UiCommands.Parse(Valid[Cmd.SelectStep], out var step, out _, out _);
        Check.That(step!.Number("half") == 1 && step.Number("direction") == -1, "commands: select.step keeps half and direction");
        UiCommands.Parse(Valid[Cmd.LobbiesGame], out var game, out _, out _);
        Check.That(game!.OptionalText("gameId") == null, "commands: lobbies.game takes null");
        UiCommands.Parse("""{"type":"view.open","view":"settings"}""", out var view, out _, out _);
        Check.That(view != null && view.OptionalText("character") == null, "commands: view.open's character is optional");
        UiCommands.Parse(Valid[Cmd.CloseConfirm], out var confirm, out _, out _);
        Check.That(confirm!.Flag("leave"), "commands: close.confirm keeps its flag");
    }

    private static void Origin()
    {
        Check.That(UiCommands.OriginAllowed("https://app.coop.example/index.html"), "origin: the app page is allowed");
        Check.That(UiCommands.OriginAllowed("https://app.coop.example/mini.html?perf=1"), "origin: a query on the app page is allowed");
        Check.That(!UiCommands.OriginAllowed("https://img.coop.example/avatar/1"), "origin: the image host is not a page");
        Check.That(!UiCommands.OriginAllowed("https://app.coop.example.evil/"), "origin: a longer host name is refused");
        Check.That(!UiCommands.OriginAllowed("http://app.coop.example/"), "origin: plain http is refused");
        Check.That(!UiCommands.OriginAllowed("https://example.com/"), "origin: another site is refused");
        Check.That(!UiCommands.OriginAllowed(null), "origin: no source is refused");
    }

    private static void DropLogLimit()
    {
        var now = 0L;
        var written = new List<string>();
        var drops = new DropLog(() => now, written.Add);
        Check.That(drops.Report("host", "x") && drops.Report("view.open", "y"), "drop log: the first drop of each type is written");
        Check.That(!drops.Report("host", "again") && written.Count == 2, "drop log: a second drop of a type within a minute is not written");
        now = DropLog.WindowMs;
        Check.That(drops.Report("host", "later") && written.Count == 3, "drop log: a type is written again after a minute");
        Check.That(written[0].Contains("host") && written[0].Contains('x'), "drop log: a line names the type and the reason");
    }

    private static void ImageRoutesAreStrict()
    {
        const string host = WebHosts.ImageOrigin;
        Check.That(ImageRoutes.Parse(host + "steam/418370/capsule") is ImageRoute.Steam { AppId: 418370, Kind: CoopLauncher.Gui.SteamArt.Kind.Capsule }, "images: steam art by app id and kind");
        Check.That(ImageRoutes.Parse(host + "steam/418370/hero") is ImageRoute.Steam { Kind: CoopLauncher.Gui.SteamArt.Kind.Hero }, "images: steam hero");
        Check.That(ImageRoutes.Parse(host + "avatar/76561198000000001?r=3") is ImageRoute.Avatar { SteamId: 76561198000000001UL }, "images: an avatar by Steam id, the revision is only a key");
        Check.That(ImageRoutes.Parse(host + "figure/re0/outfit_rebecca/costume_2") is ImageRoute.Figure { GameId: "re0", Segment: "outfit_rebecca", Value: "costume_2", IsSilhouette: false }, "images: an outfit figure");
        Check.That(ImageRoutes.Parse(host + "figure/re0/rebecca/silhouette") is ImageRoute.Figure { Segment: "rebecca", IsSilhouette: true }, "images: a silhouette figure");
        string[] refused =
        [
            "steam/abc/capsule", "steam/1/other", "steam/1/capsule/extra", "steam/../capsule", "steam/99999999999/capsule", "avatar/", "avatar/abc",
            "avatar/123456789012345678901", "avatar/1/2", "figure/re0/a", "figure/re0/a/b/c", "figure/../x/y", "figure/re0/a%2fb/c", "figure/re0/a/b c",
            "figure/re0/../etc", "", "steam", "other/1",
        ];
        foreach (var path in refused) Check.That(ImageRoutes.Parse(host + path) == null, $"images: '{path}' is refused");
        Check.That(ImageRoutes.Parse("https://app.coop.example/steam/1/capsule") == null, "images: the page host serves no images");
        Check.That(ImageRoutes.Parse("https://img.coop.example.evil/steam/1/capsule") == null, "images: a longer host name is refused");

        var figure = new CoopLauncher.Gui.Figure("rebecca", "outfit_rebecca", "costume_2", "R");
        Check.That(ImageRoutes.FigureUrl("re0", figure) == host + "figure/re0/outfit_rebecca/costume_2", "images: a figure with an outfit routes to the outfit");
        Check.That(ImageRoutes.FigureUrl("re0", figure with { Value = null }) == host + "figure/re0/rebecca/silhouette", "images: a figure with no outfit routes to the silhouette");
        Check.That(ImageRoutes.FigureUrl("re0", figure with { Value = "" }) == host + "figure/re0/rebecca/silhouette", "images: the saved outfit routes to the silhouette");
        Check.That(ImageRoutes.AvatarUrl(7, 3) == host + "avatar/7?r=3" && ImageRoutes.SteamUrl(5, CoopLauncher.Gui.SteamArt.Kind.Logo) == host + "steam/5/logo", "images: urls are built to the route shapes");
    }
}
