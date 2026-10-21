using System.Globalization;

namespace CoopLauncher.Gui;

internal static partial class SessionScreens
{
    private const int TwoSeats = 2;
    private const int RecentEvents = 2;
    private const string DetailGap = "  ";
    private const string HostWord = "Host";
    private const string YouWord = "You";
    private const string OpenName = "Open";
    private const string PingFormat = "{0} ms";

    /// <summary>The hosting screen (the host until its game starts) or the lobby (everyone once in game, and while the lobby is
    /// rebuilt). <paramref name="events"/> are the player events, oldest first; <paramref name="unixNow"/> is the clock the session
    /// timer counts against (now when omitted).</summary>
    public static SessionModel Session(AppStatus s, GameProfile g, string? gameDir, GameStatus mod, GameCondition c,
        IReadOnlyList<string> events, long nowMs, long? unixNow = null)
    {
        var local = s.Players.FirstOrDefault(player => player.IsLocal);
        var hosting = local is { IsHost: true } && s.State is not (AppState.Rejoining or AppState.WaitingForHost)
            && s.Step is not (SessionStep.InGame or SessionStep.GameClosed);
        var seats = Seats(s, g, gameDir, hosting);
        var state = hosting ? null : LobbyStateOf(s, c, unixNow ?? DateTimeOffset.UtcNow.ToUnixTimeSeconds());
        return new SessionModel(
            hosting, s.LobbyId != 0 ? s.LobbyId.ToString(CultureInfo.InvariantCulture) : string.Empty,
            hosting ? HostingSteps(s, mod, nowMs) : [], hosting && StartEnabled(s, local!), state,
            hosting ? [] : HostOptions(s, g), hosting ? [] : events.TakeLast(RecentEvents).ToList(),
            state?.Kind == LobbyStateKind.GameClosed, hosting ? $"PLAYERS {s.Players.Count}/{seats.Count}" : string.Empty,
            seats, hosting && seats.Count <= TwoSeats ? CardLayout.Wide : CardLayout.Tall, SessionCards.NoticeFor(s, 0));
    }

    /// <summary>One card per seat: the host first, then the other members, then open seats up to the seat count.</summary>
    public static IReadOnlyList<PlayerCard> Seats(AppStatus s, GameProfile g, string? gameDir) => Seats(s, g, gameDir, hosting: false);

    /// <summary>The character a card is for (the part of its detail line before the outfit); empty for a game without characters.</summary>
    public static string CharacterOf(PlayerCard card) => card.Detail.Split(DetailGap)[0];

    /// <summary>The cards of the screen: in the hosting screen the local host's corner says Host (the card is the host's own);
    /// a joining player's card carries its step list on the large cards of a two-seat game.</summary>
    private static List<PlayerCard> Seats(AppStatus s, GameProfile g, string? gameDir, bool hosting, bool compact = false)
    {
        var members = Ordered(s);
        var count = SeatCount(s, g, gameDir, hosting);
        var withSteps = !compact && count <= TwoSeats;
        var cards = new List<PlayerCard>();
        for (var seat = 0; seat < count; seat++)
        {
            var character = seat < s.SeatCharacters.Count ? s.SeatCharacters[seat] : string.Empty;
            cards.Add(seat < members.Count ? Taken(members[seat], character, g, gameDir, hosting, withSteps) : OpenCard(character));
        }
        return cards;
    }

    /// <summary>The seats the lobby takes (never fewer than its members); none while a rebuilt lobby has no members.</summary>
    private static int SeatCount(AppStatus s, GameProfile g, string? gameDir, bool hosting)
    {
        if (s.Players.Count == 0 && !hosting) return 0;
        var seats = s.Seats > 0 ? s.Seats : GameOptions.Seats(g, GameOptions.HostValues(g, gameDir));
        return Math.Max(seats, s.Players.Count);
    }

    private static PlayerCard OpenCard(string character) =>
        new(0, OpenName, string.Empty, character, string.Empty, string.Empty, CardState.Open, false, null, [], null, string.Empty);

    private static PlayerCard Taken(PlayerSlot player, string character, GameProfile g, string? gameDir, bool hosting, bool withSteps)
    {
        var corner = Corner(player);
        var hostCorner = player.IsHost && (corner.Length == 0 || (hosting && player.IsLocal));
        var outfit = OutfitValue(player, character, g, gameDir);
        return new PlayerCard(
            player.Id, player.Name, hostCorner ? HostWord : corner, DetailOf(character, outfit, g), CardStates.Words(player),
            player.IsHost && !hostCorner ? HostWord : string.Empty, CardStates.Of(player), player.IsLocal, FigureOf(character, outfit, g),
            withSteps ? JoinerSteps(player, g) : [], player.Step == SessionStep.ReceivingSave ? player.Percent / PercentScale : null,
            player.Held ? MinutesSeconds(player.HoldSecondsLeft) : string.Empty);
    }

    /// <summary>You on the local card, else the hold countdown while held, else the ping.</summary>
    private static string Corner(PlayerSlot player)
    {
        if (player.IsLocal) return YouWord;
        if (player.Held) return MinutesSeconds(player.HoldSecondsLeft);
        return player.RttMs is { } rtt ? string.Format(CultureInfo.InvariantCulture, PingFormat, rtt) : string.Empty;
    }

    /// <summary>The outfit option value a player published for a character (the local player's own choice when it published none);
    /// null when it is not known.</summary>
    private static string? OutfitValue(PlayerSlot player, string character, GameProfile g, string? gameDir)
    {
        if (!g.HasCharacterSelect || GameOptions.CharacterValue(g, character) is not { } value || GameOptions.OutfitOf(g, value) is not { } option) return null;
        var published = GameOptions.Decode(player.Options);
        if (published.TryGetValue(option.Key, out var chosen)) return chosen;
        return player.IsLocal && player.Options.Length == 0 ? GameOptions.Current(g, gameDir).GetValueOrDefault(option.Key) : null;
    }

    /// <summary>The character, then its outfit's label (a gap of two spaces); the outfit "saved" and an unknown one have none.</summary>
    private static string DetailOf(string character, string? outfit, GameProfile g)
    {
        var label = outfit is { Length: > 0 } && GameOptions.CharacterValue(g, character) is { } value
            ? GameOptions.OutfitOf(g, value)?.Values.FirstOrDefault(v => v.Value == outfit)?.Label
            : null;
        return label == null ? character : character + DetailGap + label;
    }

    private static Figure? FigureOf(string character, string? outfit, GameProfile g)
    {
        if (!g.HasCharacterSelect || GameOptions.CharacterValue(g, character) is not { } value) return null;
        return new Figure(value, GameOptions.OutfitOf(g, value)?.Key ?? string.Empty, outfit, character[..1]);
    }

    /// <summary>The mini step list of a joining player: the build check, then the save with its percent and bar.</summary>
    private static IReadOnlyList<StepRow> JoinerSteps(PlayerSlot player, GameProfile g)
    {
        if (player.Step is not (SessionStep.CheckingBuild or SessionStep.ReceivingSave)) return [];
        var receiving = player.Step == SessionStep.ReceivingSave;
        var rows = new List<StepRow> { new("Build check", receiving ? StepMark.Done : StepMark.Active) };
        if (g.SaveSync == null) return rows;
        rows.Add(receiving
            ? new StepRow("Receiving save", StepMark.Active, $"{player.Percent}%", player.Percent / PercentScale)
            : new StepRow("Receiving save", StepMark.Pending));
        return rows;
    }

    /// <summary>The host may start once its own step is Ready and nobody else is still joining.</summary>
    private static bool StartEnabled(AppStatus s, PlayerSlot local) =>
        s.Step == SessionStep.Ready && !s.Players.Any(player => player != local && player.Step is SessionStep.Lobby or SessionStep.CheckingBuild or SessionStep.ReceivingSave);

    private static IReadOnlyList<StepRow> HostingSteps(AppStatus s, GameStatus mod, long nowMs)
    {
        var opening = s.Step == SessionStep.None;
        var sending = s.Players.FirstOrDefault(player => !player.IsLocal && player.Step == SessionStep.ReceivingSave);
        var third = s.Step == SessionStep.StartingGame ? new StepRow("Starting game", StepMark.Active, SinceTimer(s.StepSinceMs, nowMs))
            : sending != null ? new StepRow($"Sending save to {sending.Name}", StepMark.Active)
            : new StepRow("Waiting for players", opening ? StepMark.Pending : StepMark.Active, opening ? string.Empty : SinceTimer(s.LobbyOpenedAtMs, nowMs));
        return
        [
            new StepRow("Lobby open", opening ? StepMark.Active : StepMark.Done),
            new StepRow(mod.Mod.Build > 0 ? $"Mod {mod.Mod.Build}" : "Mod installed", opening ? StepMark.Pending : StepMark.Done),
            third,
            new StepRow("In game", StepMark.Pending),
        ];
    }

    private static LobbyState LobbyStateOf(AppStatus s, GameCondition c, long unixNow)
    {
        if (c == GameCondition.Exited || s.Step == SessionStep.GameClosed) return new LobbyState(LobbyStateKind.GameClosed, "Game closed", string.Empty);
        return s.State switch
        {
            AppState.Rejoining => new LobbyState(LobbyStateKind.Reconnecting, "Reconnecting", MinutesSeconds(s.SecondsLeft)),
            AppState.WaitingForHost => new LobbyState(LobbyStateKind.WaitingForHost, "Waiting for host", MinutesSeconds(s.SecondsLeft)),
            _ => new LobbyState(LobbyStateKind.InGame, "In game", s.SessionStartedUnix > 0 ? HoursMinutesSeconds(unixNow - s.SessionStartedUnix) : string.Empty),
        };
    }

    /// <summary>The host's options as label and value, as the lobby carries them (the character choice is the select view's, not
    /// listed here).</summary>
    private static IReadOnlyList<(string Label, string Value)> HostOptions(AppStatus s, GameProfile g) =>
        (g.Options ?? [])
            .Where(option => option.Host && !GameOptions.InCharacterSelect(g, option))
            .Select(option => (Option: option, Value: option.Values.FirstOrDefault(v => s.HostOptions.TryGetValue(option.Key, out var key) && v.Value == key)))
            .Where(pair => pair.Value != null)
            .Select(pair => (Label: pair.Option.Label, Value: pair.Value!.Label))
            .ToList();
}
