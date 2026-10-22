using System.Text;
using System.Text.Json;

namespace CoopLauncher.Web;

/// <summary>The names of the page's commands (SPEC 3.3). <c>js/bridge.js</c> lists the same names in <c>COMMANDS</c>; a test compares.</summary>
internal static class Cmd
{
    public const string Ready = "ready";
    public const string PageRendered = "page.rendered";
    public const string WindowMinimize = "window.minimize";
    public const string WindowClose = "window.close";
    public const string CloseConfirm = "close.confirm";
    public const string TabSelect = "tab.select";
    public const string GameSelect = "game.select";
    public const string Host = "host";
    public const string JoinCode = "join.code";
    public const string JoinFriend = "join.friend";
    public const string LobbyOpen = "lobby.open";
    public const string LobbyAction = "lobby.action";
    public const string LobbiesFilter = "lobbies.filter";
    public const string LobbiesGame = "lobbies.game";
    public const string PopupOptions = "popup.options";
    public const string PopupClose = "popup.close";
    public const string OptionSet = "option.set";
    public const string ModInstall = "mod.install";
    public const string ModRetry = "mod.retry";
    public const string NoticeFix = "notice.fix";
    public const string ReportCreate = "report.create";
    public const string SessionCancel = "session.cancel";
    public const string SessionStart = "session.start";
    public const string SessionLeave = "session.leave";
    public const string SessionCopyCode = "session.copyCode";
    public const string SessionRelaunch = "session.relaunch";
    public const string SessionMini = "session.mini";
    public const string SessionPlaySolo = "session.playSolo";
    public const string MiniExpand = "mini.expand";
    public const string MiniClosed = "mini.closed";
    public const string ViewOpen = "view.open";
    public const string ViewClose = "view.close";
    public const string SelectStep = "select.step";
    public const string SelectSwap = "select.swap";
    public const string SettingsUpdates = "settings.updates";
    public const string SettingsVisibility = "settings.visibility";
    public const string SettingsLogs = "settings.logs";
    public const string GameBrowse = "game.browse";
    public const string GameResetFolder = "game.resetFolder";
    public const string GameUninstall = "game.uninstall";
    public const string GameArchive = "game.archive";
    public const string UpdateClick = "update.click";
    public const string UpdateRestart = "update.restart";
}

/// <summary>How a field of a command is read: text, a lobby id as digits, a flag, a whole number in a range, a direction (-1 or 1), or
/// text that may be null.</summary>
internal enum FieldKind { Text, Digits, Flag, Number, Sign, NullableText }

/// <summary>One field of a command: its name, kind, whether it may be left out, and the values an enumerated field allows (text) or
/// the range a number may take.</summary>
internal sealed record Field(string Name, FieldKind Kind, bool Required = true, string[]? Choices = null, int Min = 0, int Max = UiCommands.MaxText);

/// <summary>A command that passed <see cref="UiCommands.Parse"/>: its type and its fields, each read with the kind the table says.</summary>
internal sealed record UiCommand(string Type, IReadOnlyDictionary<string, object?> Values)
{
    public string Text(string field) => (string)Values[field]!;

    public string? OptionalText(string field) => Values.TryGetValue(field, out var value) ? value as string : null;

    public bool Flag(string field) => (bool)Values[field]!;

    public int Number(string field) => (int)Values[field]!;

    /// <summary>A lobby id, from the digits the page sent.</summary>
    public ulong LobbyId() => ulong.Parse(Text("lobbyId"), System.Globalization.CultureInfo.InvariantCulture);
}

/// <summary>Parsing and validating what the page sends (SPEC 3.3): a message under 4 KB, an object at most 3 levels deep, a known
/// type, and exactly the fields of that type with the right kinds. What a command may name right now (a row on show, a character
/// of the game) is the controller's to check.</summary>
internal static class UiCommands
{
    public const int MaxBytes = 4096;
    public const int MaxDepth = 3;
    public const int MaxText = 64;
    public const int MaxDigits = 20;

    private static readonly Field GameId = new("gameId", FieldKind.Text, Min: 1);
    private static readonly Field LobbyId = new("lobbyId", FieldKind.Digits);
    private static readonly string[] Tabs = ["play", "lobbies"];
    private static readonly string[] Filters = ["all", "friends"];
    private static readonly string[] Views = ["settings", "select"];
    private static readonly string[] Visibilities = ["friends", "everyone"];

    private static readonly Dictionary<string, Field[]> Table = new()
    {
        [Cmd.Ready] = [],
        [Cmd.PageRendered] = [new("seq", FieldKind.Number, Max: int.MaxValue)],
        [Cmd.WindowMinimize] = [],
        [Cmd.WindowClose] = [],
        [Cmd.CloseConfirm] = [new("leave", FieldKind.Flag)],
        [Cmd.TabSelect] = [new("tab", FieldKind.Text, Choices: Tabs)],
        [Cmd.GameSelect] = [GameId],
        [Cmd.Host] = [],
        [Cmd.JoinCode] = [new("code", FieldKind.Text)],
        [Cmd.JoinFriend] = [LobbyId],
        [Cmd.LobbyOpen] = [LobbyId],
        [Cmd.LobbyAction] = [LobbyId],
        [Cmd.LobbiesFilter] = [new("filter", FieldKind.Text, Choices: Filters)],
        [Cmd.LobbiesGame] = [new("gameId", FieldKind.NullableText)],
        [Cmd.PopupOptions] = [],
        [Cmd.PopupClose] = [],
        [Cmd.OptionSet] = [GameId, new("key", FieldKind.Text, Min: 1), new("value", FieldKind.Text, Min: 1)],
        [Cmd.ModInstall] = [GameId],
        [Cmd.ModRetry] = [GameId],
        [Cmd.NoticeFix] = [],
        [Cmd.ReportCreate] = [],
        [Cmd.SessionCancel] = [],
        [Cmd.SessionStart] = [],
        [Cmd.SessionLeave] = [],
        [Cmd.SessionCopyCode] = [],
        [Cmd.SessionRelaunch] = [],
        [Cmd.SessionMini] = [],
        [Cmd.SessionPlaySolo] = [],
        [Cmd.MiniExpand] = [],
        [Cmd.MiniClosed] = [],
        [Cmd.ViewOpen] = [new("view", FieldKind.Text, Choices: Views), new("character", FieldKind.Text, Required: false, Min: 1)],
        [Cmd.ViewClose] = [],
        [Cmd.SelectStep] = [new("half", FieldKind.Number, Max: 1), new("direction", FieldKind.Sign)],
        [Cmd.SelectSwap] = [],
        [Cmd.SettingsUpdates] = [new("on", FieldKind.Flag)],
        [Cmd.SettingsVisibility] = [new("value", FieldKind.Text, Choices: Visibilities)],
        [Cmd.SettingsLogs] = [],
        [Cmd.GameBrowse] = [GameId],
        [Cmd.GameResetFolder] = [GameId],
        [Cmd.GameUninstall] = [GameId],
        [Cmd.GameArchive] = [GameId],
        [Cmd.UpdateClick] = [],
        [Cmd.UpdateRestart] = [],
    };

    /// <summary>Every command type, as <c>bridge.js</c> must list them.</summary>
    public static IReadOnlyCollection<string> Types => Table.Keys;

    /// <summary>A message may be acted on only when it comes from the app page: the source is a document on the app host.</summary>
    public static bool OriginAllowed(string? source) => source != null && source.StartsWith(WebHosts.AppOrigin, StringComparison.Ordinal);

    /// <summary>Reads one message. On success <paramref name="command"/> is set; otherwise <paramref name="error"/> says why it is
    /// dropped (and <paramref name="typeName"/> is the type it claimed, when that was readable, for the drop log).</summary>
    public static bool Parse(string json, out UiCommand? command, out string error, out string typeName)
    {
        command = null;
        typeName = string.Empty;
        if (Encoding.UTF8.GetByteCount(json) >= MaxBytes) return Fail("message too large", out error);
        try
        {
            using var document = JsonDocument.Parse(json, new JsonDocumentOptions { MaxDepth = MaxDepth });
            var root = document.RootElement;
            if (root.ValueKind != JsonValueKind.Object) return Fail("not an object", out error);
            if (!root.TryGetProperty("type", out var type) || type.ValueKind != JsonValueKind.String) return Fail("no type", out error);
            typeName = Clip(type.GetString()!);
            if (!Table.TryGetValue(type.GetString()!, out var fields)) return Fail("unknown type", out error);
            return Read(type.GetString()!, root, fields, out command, out error);
        }
        catch (JsonException)
        {
            return Fail("not valid JSON", out error);
        }
    }

    private static bool Read(string type, JsonElement root, Field[] fields, out UiCommand? command, out string error)
    {
        command = null;
        var values = new Dictionary<string, object?>();
        foreach (var property in root.EnumerateObject())
            if (!property.NameEquals("type") && !fields.Any(field => property.NameEquals(field.Name))) return Fail($"unexpected field {Clip(property.Name)}", out error);
        foreach (var field in fields)
        {
            if (!root.TryGetProperty(field.Name, out var element))
            {
                if (field.Required) return Fail($"missing {field.Name}", out error);
                continue;
            }
            if (!ReadValue(field, element, out var value)) return Fail($"bad {field.Name}", out error);
            values[field.Name] = value;
        }
        command = new UiCommand(type, values);
        error = string.Empty;
        return true;
    }

    private static bool ReadValue(Field field, JsonElement element, out object? value)
    {
        value = null;
        switch (field.Kind)
        {
            case FieldKind.Flag when element.ValueKind is JsonValueKind.True or JsonValueKind.False:
                value = element.GetBoolean();
                return true;
            case FieldKind.Number when element.ValueKind == JsonValueKind.Number && element.TryGetInt32(out var number):
                value = number;
                return number >= field.Min && number <= field.Max;
            case FieldKind.Sign when element.ValueKind == JsonValueKind.Number && element.TryGetInt32(out var sign):
                value = sign;
                return sign is -1 or 1;
            case FieldKind.NullableText when element.ValueKind == JsonValueKind.Null:
                return true;
            case FieldKind.Digits when element.ValueKind == JsonValueKind.String:
                var digits = element.GetString()!;
                value = digits;
                return digits.Length is > 0 and <= MaxDigits && digits.All(char.IsAsciiDigit) && ulong.TryParse(digits, out _);
            case FieldKind.Text or FieldKind.NullableText when element.ValueKind == JsonValueKind.String:
                var text = element.GetString()!;
                value = text;
                return text.Length >= field.Min && text.Length <= field.Max && (field.Choices == null || field.Choices.Contains(text));
            default:
                return false;
        }
    }

    private static bool Fail(string reason, out string error)
    {
        error = reason;
        return false;
    }

    private static string Clip(string text) => text.Length <= MaxText ? text : text[..MaxText];
}

/// <summary>The log of dropped messages: file only, and a type is reported at most once per minute (a page in a loop must not
/// flood the log).</summary>
internal sealed class DropLog
{
    public const long WindowMs = 60_000;

    private readonly Dictionary<string, long> _lastLogged = new();
    private readonly Func<long> _clock;
    private readonly Action<string> _write;

    public DropLog(Func<long> clock, Action<string> write)
    {
        _clock = clock;
        _write = write;
    }

    public static DropLog ToLogFile() => new(() => Environment.TickCount64, Log.FileOnly);

    /// <summary>Notes a dropped message of this type; true when it was written to the log (the first of its type this minute).</summary>
    public bool Report(string type, string reason)
    {
        var now = _clock();
        if (_lastLogged.TryGetValue(type, out var last) && now - last < WindowMs) return false;
        _lastLogged[type] = now;
        _write($"Page message dropped ({(type.Length == 0 ? "no type" : type)}): {reason}");
        return true;
    }
}
