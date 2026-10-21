using CoopLauncher.Gui;

namespace CoopLauncher.Web;

/// <summary>One game of the rail: its profile, what its mod state says and the art routes it has (null where a picture is missing).</summary>
internal sealed record GameEntry(GameProfile Profile, GameStatus Status, ArtDto Art);

/// <summary>Which popup is open: none, the options of the selected game, or the lobby popup of one listed lobby.</summary>
internal enum PopupKind { None, Options, Lobby }

/// <summary>The update button as the top bar shows it: its label (null while there is nothing to offer), whether it can be
/// clicked, the newer build on offer (0 when none) and whether a build is staged.</summary>
internal sealed record UpdateView(string? Label, bool Enabled, int NewerBuild, bool Staged);

/// <summary>Everything a snapshot is made from: the controller's state and the world around it, so that the mapping is a pure
/// function that tests feed with fake statuses.</summary>
internal sealed record SnapshotInput(
    AppStatus Status, IReadOnlyList<GameEntry> Games, string? SelectedId, ScreenKind Screen, ScreenKind? View, Tab Tab,
    PopupKind Popup, ulong PopupLobbyId, LobbyFilter Filter, string? LobbyGameId, JoinTarget? Target, GameCondition Condition,
    UpdateView Update, IReadOnlyList<string> Events, string BuildLabel, int LocalBuild, int? SelectFocus, long NowMs, long UnixNow,
    Func<GameProfile, string?> FolderOf, Func<ulong, int?> AvatarRevision)
{
    public bool Free => Status.State is AppState.Idle or AppState.Offline;

    public IEnumerable<GameProfile> Profiles => Games.Select(game => game.Profile);

    public GameEntry? Selected => Games.FirstOrDefault(game => game.Profile.Id == SelectedId);

    /// <summary>The game a screen is about: the open lobby's (or the refused lobby's), else the one selected in the rail.</summary>
    public GameEntry? GameOf() => Games.FirstOrDefault(game => game.Profile.Id == (Status.GameId ?? Status.Refusal?.GameId)) ?? Selected;

    public GameStatus ModOf(string gameId) => Games.FirstOrDefault(game => game.Profile.Id == gameId)?.Status ?? GameStatus.Unknown;

    public ArtDto ArtOf(string gameId) => Games.FirstOrDefault(game => game.Profile.Id == gameId)?.Art ?? UiSnapshot.NoArt;

    /// <summary>The rows of the Lobbies tab for this input (also the rows a command may name while the tab shows).</summary>
    public IReadOnlyList<LobbyRowModel> LobbyRows() =>
        LobbyRowModels.Of(Status, Filter, LobbyGameId, Games.Select(game => game.Profile).ToList(), ModOf, LocalBuild, Update.NewerBuild);
}

/// <summary>The pure mapping from the controller's state to the JSON the pages are sent (SPEC 3.2): only the output of the
/// existing mappers, in the page's shape. Unit tested (<c>UiSnapshotTests</c>).</summary>
internal static partial class UiSnapshot
{
    public static readonly ArtDto NoArt = new(null, null, null);

    /// <summary>The main page's state. <paramref name="lastPosted"/> is the screen of the snapshot posted before this one, which
    /// the transition runs from.</summary>
    public static MainState MainPage(SnapshotInput i, ScreenKind? lastPosted)
    {
        var game = i.GameOf();
        var screen = i.Screen;
        var transition = lastPosted is { } from ? Screens.Between(from, screen) : new Transition(TransitionKind.None);
        var running = i.Status.State is AppState.GameRunning or AppState.PeerConnected ? i.Status.GameId : null;
        return new MainState(
            screen, new TransitionDto(transition.Kind, transition.Direction), Top(i, screen), i.Games.Select(entry => RailGame(i, entry, running)).ToArray(),
            game?.Art ?? NoArt, new FooterDto(i.Events.LastOrDefault() ?? string.Empty), PopupOf(i),
            screen == ScreenKind.Home ? Home(i) : null,
            screen == ScreenKind.Lobbies ? Lobbies(i) : null,
            screen == ScreenKind.Joining && game != null ? Joining(i, game) : null,
            screen is ScreenKind.Hosting or ScreenKind.Lobby && game != null ? Session(i, game) : null,
            screen == ScreenKind.Select && game != null ? Select(i, game) : null,
            screen == ScreenKind.Settings ? Settings(i) : null);
    }

    /// <summary>A stable image for a Steam id: the avatar route once the avatar arrived, else the initial alone.</summary>
    internal static ImgDto Avatar(SnapshotInput i, ulong steamId, string name) =>
        new(steamId != 0 && i.AvatarRevision(steamId) is { } revision ? ImageRoutes.AvatarUrl(steamId, revision) : null, Initials.Of(name));

    private static TopDto Top(SnapshotInput i, ScreenKind screen)
    {
        var mode = screen is ScreenKind.Home or ScreenKind.Lobbies ? TopMode.Browse : i.View != null ? TopMode.View : TopMode.Session;
        return new TopDto(mode, i.Tab, i.BuildLabel, i.Update.Label is { } label ? new BtnDto(label, i.Update.Enabled) : null);
    }

    private static RailGameDto RailGame(SnapshotInput i, GameEntry entry, string? runningId)
    {
        var selected = entry.Profile.Id == i.SelectedId;
        return new RailGameDto(
            entry.Profile.Id, entry.Profile.Name, entry.Art.Capsule, RailModel.Sub(entry.Profile, runningId), entry.Status.Line,
            RailModel.Tone(entry.Status), RailModel.Locked(i.Free, selected), selected);
    }

    private static object? PopupOf(SnapshotInput i) => i.Popup switch
    {
        PopupKind.Options when i.Selected is { } selected => OptionsPopup(i, selected),
        PopupKind.Lobby when i.Screen == ScreenKind.Lobbies => LobbyPopup(i),
        _ => null,
    };

    private static OptionsPopupDto OptionsPopup(SnapshotInput i, GameEntry game)
    {
        var rows = OptionRows(i, game.Profile, settingsOptions: false, i.Free ? null : i.Status.HostOptions);
        return new OptionsPopupDto("options", game.Profile.Name, game.Art, !i.Free, rows);
    }

    private static OptionRowDto[] OptionRows(SnapshotInput i, GameProfile game, bool settingsOptions, IReadOnlyDictionary<string, string>? hostValues)
    {
        var folder = i.FolderOf(game);
        return OptionsModel.OptionsOf(game, settingsOptions).Select(option => new OptionRowDto(
            option.Key, option.Label, OptionsModel.ShownValue(game, option, folder, hostValues),
            OptionsModel.Values(option, folder).Select(value => new OptionValueDto(value.Value, value.Label)).ToArray(), !i.Free)).ToArray();
    }

    private static LobbyPopupDto? LobbyPopup(SnapshotInput i)
    {
        var model = i.LobbyRows().FirstOrDefault(row => row.Listing.LobbyId == i.PopupLobbyId);
        if (model == null) return null;
        var listing = model.Listing;
        var mod = i.ModOf(model.Game.Id);
        var seats = LobbyPopupModel.Seats(listing);
        var roster = seats.Where(seat => seat.Kind != PopupSeatKind.Open).Select((seat, index) => new RosterEntryDto(
            index.ToString(), new ImgDto(null, Initials.Of(seat.Entry?.Name ?? string.Empty)), seat.Entry?.Name ?? string.Empty, seat.Host,
            Names.Of(seat.Entry == null ? CardState.Idle : CardStates.OfStep(seat.Entry.Step)))).ToArray();
        return new LobbyPopupDto(
            "lobby", UiJson.Id(listing.LobbyId), i.ArtOf(model.Game.Id), LobbyPopupModel.Caption(listing), roster,
            seats.Count(seat => seat.Kind == PopupSeatKind.Open), LobbyRowModels.Ping(listing),
            new CheckedDto(!model.ModDiffers, listing.ModBuild > 0 ? listing.ModBuild.ToString(System.Globalization.CultureInfo.InvariantCulture) : string.Empty),
            new CheckedDto(LobbyPopupModel.GameOk(listing, mod), listing.GameBuild > 0 ? $"Build {listing.GameBuild}" : string.Empty),
            LobbyPopupModel.Network(model), LobbyPopupModel.PlayingText(listing.StartedUnix, i.UnixNow), Button(model.Action));
    }

    private static LobbyBtnDto Button(LobbyAction action)
    {
        var button = LobbyRowModels.ButtonOf(action);
        return new LobbyBtnDto(button.Label, button.Primary, button.Enabled);
    }

    private static HomeDto? Home(SnapshotInput i)
    {
        if (i.Selected is not { } selected) return null;
        var view = SessionCards.Home(i.Status, selected.Profile, selected.Status, i.Update.Staged, i.Update.NewerBuild, i.Profiles);
        return new HomeDto(
            view.Native ? "LAUNCH" : "HOST", view.Native, view.HostEnabled, view.JoinEnabled, view.CodeEnabled,
            view.Mod is { } mod ? new BtnDto(mod.Label, view.ModEnabled) : null, OptionsModel.Has(selected.Profile), selected.Profile.HasCharacterSelect,
            i.Popup == PopupKind.Options, view.Notice == null ? null : Notice(view.Notice),
            view.Friends.Select(friend => new FriendDto(UiJson.Id(friend.LobbyId), Avatar(i, friend.AvatarId, friend.Name), friend.Name, friend.Game, friend.Count, friend.Selected)).ToArray());
    }

    internal static NoticeDto Notice(Notice notice) => new(notice.Key, notice.Words, notice.Action, notice.Report);

    private static LobbiesDto Lobbies(SnapshotInput i)
    {
        var rows = i.LobbyRows().Select(model =>
        {
            var listing = model.Listing;
            return new LobbyRowDto(
                UiJson.Id(listing.LobbyId), i.ArtOf(model.Game.Id).Capsule, model.Game.Name,
                new LobbyHostDto(listing.HostName, Avatar(i, listing.HostId, listing.HostName)), LobbyRowModels.Players(listing),
                new LobbyModDto(listing.ModBuild > 0 ? listing.ModBuild.ToString(System.Globalization.CultureInfo.InvariantCulture) : string.Empty, model.ModDiffers),
                LobbyRowModels.Ping(listing), Button(model.Action));
        }).ToArray();
        return new LobbiesDto(
            i.Filter, i.LobbyGameId, i.Games.Select(game => new LobbyGameDto(game.Profile.Id, game.Profile.Name)).ToArray(),
            i.Status.LobbiesSearching, rows);
    }

    private static SettingsDto Settings(SnapshotInput i)
    {
        var games = i.Games.Select(game => new SettingsGameDto(
            game.Profile.Id, game.Profile.Name, i.FolderOf(game.Profile) ?? string.Empty, SettingsModel.BrowseEnabled(i.Free),
            SettingsModel.ResetEnabled(game.Profile, i.Free), SettingsModel.UninstallEnabled(game.Status, i.Free, i.Update.Staged),
            game.Status.SettingsMod, game.Status.SettingsGame, OptionRows(i, game.Profile, settingsOptions: true, hostValues: null), SettingsModel.ArchiveShown(game.Profile))).ToArray();
        return new SettingsDto(AppSettings.Current.CheckForUpdates, AppSettings.Current.LobbyVisibility, !i.Free, games);
    }
}
