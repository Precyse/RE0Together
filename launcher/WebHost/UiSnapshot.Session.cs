using CoopLauncher.Gui;

namespace CoopLauncher.Web;

/// <summary>The wire names of the few enums whose page spelling is not their camelCase name.</summary>
internal static class Names
{
    public static string Of(CardState state) => state == CardState.InGame ? "ingame" : state.ToString().ToLowerInvariant();

    public static string Of(HalfKind kind) => kind == HalfKind.ReadOnly ? "readonly" : kind.ToString().ToLowerInvariant();
}

internal static partial class UiSnapshot
{
    private static JoiningDto Joining(SnapshotInput i, GameEntry game)
    {
        var model = SessionScreens.Joining(i.Status, game.Profile, game.Status, i.Target, i.Condition, i.Update.NewerBuild, i.NowMs);
        return new JoiningDto(
            model.Title, model.Sub, Avatar(i, model.HostId, model.HostName), Steps(model.Steps),
            model.Fix == NoticeFix.None ? null : new BtnDto(model.FixLabel, model.FixEnabled), model.StartAgain);
    }

    private static SessionDto Session(SnapshotInput i, GameEntry game)
    {
        var model = SessionScreens.Session(i.Status, game.Profile, i.FolderOf(game.Profile), game.Status, i.Condition, i.Events, i.NowMs, i.UnixNow);
        return new SessionDto(
            model.Hosting, model.IsHost, model.Code, Steps(model.Steps), model.StartEnabled,
            model.State is { } state ? new LobbyStateDto(state.Kind, state.Words, state.Timer) : null,
            model.Options.Select(option => new[] { option.Label, option.Value }).ToArray(), model.Events.ToArray(), model.Relaunch, model.Counter,
            model.Layout, Cards(i, game.Profile.Id, model.Seats), model.Seats.Any(card => card.Local && card.Figure != null),
            model.Notice == null ? null : Notice(model.Notice), SavePicker(i, game.Profile, model));
    }

    /// <summary>The host's save picker on the hosting screen: the slots of its save, the chosen one selected; none for a guest, a game
    /// without one or once the host's game starts.</summary>
    private static OptionRowDto? SavePicker(SnapshotInput i, GameProfile game, SessionModel model)
    {
        var folder = i.FolderOf(game);
        if (!model.Hosting || !model.IsHost || OptionsModel.SavePicker(game, folder, i.Status.Step) is not { } option) return null;
        return new OptionRowDto(
            option.Key, option.Label, GameOptions.Current(game, folder)[option.Key],
            GameOptions.Offered(game, option, folder).Select(value => new OptionValueDto(value.Value, value.Label)).ToArray(), false);
    }

    /// <summary>The mini page's state for the game the session is about.</summary>
    public static MiniState MiniPage(SnapshotInput i)
    {
        if (i.GameOf() is not { } game) return new MiniState(NoArt, new MiniDto(string.Empty, new MiniHeaderDto(MiniHeaderKind.Ping, string.Empty), CardLayout.Mini, [], null, string.Empty, false));
        var model = SessionScreens.Mini(i.Status, game.Profile, i.FolderOf(game.Profile), i.Events);
        var alert = model.Alert is { } shown
            ? new MiniAlertDto(Card(i, game.Profile.Id, shown.Card, SeatIndexOf(model.Seats, shown.Card)), shown.Words, shown.Remaining, shown.Holding)
            : null;
        return new MiniState(
            game.Art, new MiniDto(
                model.GameName, new MiniHeaderDto(model.Header.Kind, model.Header.Text), model.Layout, Cards(i, game.Profile.Id, model.Seats), alert,
                model.Event, model.PlaySolo));
    }

    private static int SeatIndexOf(IReadOnlyList<PlayerCard> seats, PlayerCard card)
    {
        for (var seat = 0; seat < seats.Count; seat++)
            if (ReferenceEquals(seats[seat], card)) return seat;
        return 0;
    }

    private static StepDto[] Steps(IReadOnlyList<StepRow> rows) => rows.Select(row => new StepDto(
        row.Text, row.Mark, row.Trailing, row.Bar, row.Indeterminate, row.Lines?.ToArray() ?? [],
        row.Pairs?.Select(pair => new[] { pair.Label, pair.Value }).ToArray() ?? [])).ToArray();

    private static CardDto[] Cards(SnapshotInput i, string gameId, IReadOnlyList<PlayerCard> cards) =>
        cards.Select((card, seat) => Card(i, gameId, card, seat)).ToArray();

    private static CardDto Card(SnapshotInput i, string gameId, PlayerCard card, int seat)
    {
        var open = card.State == CardState.Open;
        return new CardDto(
            seat.ToString(System.Globalization.CultureInfo.InvariantCulture), card.AvatarId == 0 ? string.Empty : UiJson.Id(card.AvatarId),
            Avatar(i, card.AvatarId, open ? string.Empty : card.Name), card.Figure == null ? null : new ImgDto(ImageRoutes.FigureUrl(gameId, card.Figure), card.Figure.Initial),
            card.Name, card.Corner, card.Detail, card.Words, card.Footer, Names.Of(card.State), card.Local, Steps(card.Steps), card.RightText, card.Progress,
            seat + 1, card.Unit, card.Agent, card.Outfit, card.Ready);
    }

    private static SelectDto Select(SnapshotInput i, GameEntry game)
    {
        var model = SelectModels.Build(game.Profile, i.FolderOf(game.Profile), i.Status, i.Free);
        return new SelectDto(Half(game.Profile.Id, model.Left), Half(game.Profile.Id, model.Right), model.Centre, i.SelectFocus ?? model.OwnHalf);
    }

    private static HalfDto Half(string gameId, StageHalf half) => new(
        new ImgDto(ImageRoutes.FigureUrl(gameId, half.Figure), half.Figure.Initial), half.Name, half.Role, half.Label, Names.Of(half.Kind),
        half.Locked, half.DotCount, half.Index, half.CanCycle, half.Opacity);
}
