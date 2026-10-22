using CoopLauncher.Gui;

namespace CoopLauncher.Web;

/// <summary>The state and the actions of the launcher window without any controls: the games and their mod states, the selected game, the tab, what the player asked for, the
/// open view and popup, the lobby filter, the player events, the update path, the game watch and the mini mode. It raises
/// <see cref="Changed"/> when the page needs a new snapshot. Everything here runs on the UI thread; events of the app arrive
/// through the <c>post</c> function given to the constructor. This part holds the state and what follows from a status; the
/// actions are in <c>UiController.Actions.cs</c>.</summary>
internal sealed partial class UiController : IDisposable
{
    private const int KeptEvents = 50;
    private const long ModRefreshMinMs = 2000;
    private const string DevBuildLabel = "Dev build";

    private readonly App _app;
    private readonly Action<Action> _post;
    private readonly TaskScheduler _ui = TaskScheduler.FromCurrentSynchronizationContext();
    private readonly GameWatch _watch = new();
    private readonly UpdateFlow _update = new();
    private readonly List<string> _events = new();
    private readonly Dictionary<string, ModRunView> _modRuns = new();
    private readonly string _buildLabel = BuildLabel();
    private List<GameEntry> _games = new();

    private string? _selectedId;
    private Tab _tab;
    private Intent _intent;
    private JoinTarget? _target;
    private ScreenKind? _view;
    private ScreenKind _screen = ScreenKind.Home;
    private ScreenKind? _lastPosted;
    private PopupKind _popup;
    private ulong _popupLobbyId;
    private LobbyFilter _filter;
    private string? _lobbyGameId;
    private int? _selectFocus;
    private bool _miniOpen;
    private bool _watchingLobbies;
    private bool _applying;
    private bool _sessionOpen;
    private bool _refreshing;
    private bool _refreshAgain;
    private long _lastRefreshMs;
    private bool _disposed;

    public UiController(App app, Action<Action> post)
    {
        _app = app;
        _post = post;
        LoadGames();
        RefreshMods();
        Apply(app.Status);
        _update.Changed += () =>
        {
            if (!_applying) Apply(_app.Status);
        };
        _update.RestartRequested += () =>
        {
            Updater.RestartRequested = true;
            RestartRequested?.Invoke();
        };
        _app.AvatarLoaded += OnAvatarLoaded;
        _app.StatusChanged += OnStatusChanged;
        _app.InviteAccepted += OnInviteAccepted;
        _app.ResendAvatars();
    }

    /// <summary>The page needs a new snapshot.</summary>
    public event Action? Changed;

    /// <summary>The player asked for a page event (the code field shakes, the copy icon swaps).</summary>
    public event Action<string>? PageEvent;

    /// <summary>An invite was accepted: the window comes forward and, behind other windows, flashes.</summary>
    public event Action? InviteAccepted;

    /// <summary>The Restart button: the staged build starts once the window has closed.</summary>
    public event Action? RestartRequested;

    public AvatarImages Avatars { get; } = new();

    public IReadOnlyList<GameProfile> Profiles { get; private set; } = [];

    /// <summary>The mini window is open (the main window hides while it shows).</summary>
    public bool MiniOpen => _miniOpen;

    /// <summary>Whether the second-by-second clock is needed: a session screen or the lobby popup shows times.</summary>
    public bool NeedsClock => !Free || _popup == PopupKind.Lobby;

    /// <summary>The player events, oldest first (at most <see cref="KeptEvents"/>).</summary>
    public IReadOnlyList<string> Events => _events;

    /// <summary>Whether closing the window leaves a session.</summary>
    public bool InSession => !Free;

    private bool Free => _app.Status.State is AppState.Idle or AppState.Offline;

    /// <summary>The main page's state for the screen on show; the transition runs from the last screen posted.</summary>
    public MainState BuildMain()
    {
        var state = UiSnapshot.MainPage(Input(), _lastPosted);
        _lastPosted = state.Screen;
        return state;
    }

    public MiniState BuildMini() => UiSnapshot.MiniPage(Input());

    /// <summary>The window came to the front: mod states are read again (a game closed or an install happened meanwhile).</summary>
    public void Activated() => RefreshModsThrottled();

    /// <summary>The window is shown for the first time: the release is looked up.</summary>
    public void Shown() => _update.Check(automatic: AppSettings.Current.CheckForUpdates);

    /// <summary>Once a second while a session is open or the lobby popup shows: notices the game of an open session exiting, or
    /// running without the adapter link, and refreshes what counts time.</summary>
    public void Tick()
    {
        var status = _app.Status;
        var game = GameOf(status);
        var changed = _watch.Update(status, game?.Profile, Environment.TickCount64);
        if (changed && game != null && _watch.Condition == GameCondition.Exited) Log.Event($"{game.Profile.Name} exited");
        if (changed && game != null && _watch.Condition == GameCondition.Unlinked) Log.Info($"{game.Profile.Name} is running without the co-op link (started outside the launcher?)");
        if (changed || !Free) Apply(status);
        else Changed?.Invoke();
    }

    public void AddEvent(string message)
    {
        _events.Add(message);
        if (_events.Count > KeptEvents) _events.RemoveAt(0);
        Apply(_app.Status);
    }

    /// <summary>Seeds the events logged before the window existed.</summary>
    public void SeedEvents(IEnumerable<string> history) => _events.AddRange(history.TakeLast(KeptEvents));

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        _app.StatusChanged -= OnStatusChanged;
        _app.AvatarLoaded -= OnAvatarLoaded;
        _app.InviteAccepted -= OnInviteAccepted;
    }

    internal SnapshotInput Input()
    {
        var update = new UpdateView(_update.Label, _update.Enabled, _update.NewerBuild, Updater.Staged);
        return new SnapshotInput(
            _app.Status, _games, _selectedId, _screen, _view, _tab, _popup, _popupLobbyId, _filter, _lobbyGameId, _target, _watch.Condition,
            update, _events, _buildLabel, BuildCheck.LocalBuild(), _selectFocus, Environment.TickCount64, DateTimeOffset.UtcNow.ToUnixTimeSeconds(),
            GameFolders.Find, Avatars.Revision, _modRuns);
    }

    private static string BuildLabel()
    {
        var build = BuildCheck.LocalBuild();
        return build > BuildInfo.DevBuild ? $"Build {build}" : DevBuildLabel;
    }

    /// <summary>Fills the rail with the games whose profile can be read; one unreadable profile is logged and skipped. The game of
    /// the last session is selected first.</summary>
    private void LoadGames()
    {
        var profiles = new List<GameProfile>();
        foreach (var id in GameProfile.ListIds())
        {
            try
            {
                profiles.Add(GameProfile.Load(id));
            }
            catch (Exception e) when (e is IOException or System.Text.Json.JsonException or InvalidDataException or TypeInitializationException)
            {
                Log.Info($"Game profile {id} unreadable: {e.Message}");
            }
        }
        Profiles = profiles;
        _games = profiles.Select(profile => new GameEntry(profile, GameStatus.Unknown, ArtOf(profile))).ToList();
        _selectedId = _games.FirstOrDefault()?.Profile.Id;
        if (AppSettings.Current.LastGame is { } last && _games.Any(game => game.Profile.Id == last)) _selectedId = last;
    }

    private static ArtDto ArtOf(GameProfile profile) => new(
        ArtUrl(profile, SteamArt.Kind.Hero), ArtUrl(profile, SteamArt.Kind.Logo), ArtUrl(profile, SteamArt.Kind.Capsule));

    private static string? ArtUrl(GameProfile profile, SteamArt.Kind kind) =>
        SteamArt.FindFile(profile.SteamAppId, kind) == null ? null : ImageRoutes.SteamUrl(profile.SteamAppId, kind);

    /// <summary>The game a screen is about: the open lobby's (or the refused lobby's), else the one selected in the rail.</summary>
    private GameEntry? GameOf(AppStatus status) =>
        _games.FirstOrDefault(game => game.Profile.Id == (status.GameId ?? status.Refusal?.GameId)) ?? SelectedGame;

    private GameEntry? SelectedGame => _games.FirstOrDefault(game => game.Profile.Id == _selectedId);

    // ---------------------------------------------------------------- showing a status

    private void Apply(AppStatus status)
    {
        _applying = true;
        try
        {
            if (status.GameId is { } lobbyGame && _games.Any(game => game.Profile.Id == lobbyGame)) _selectedId = lobbyGame;
            var free = status.State is AppState.Idle or AppState.Offline;
            _screen = ScreenFor(status);
            if (_sessionOpen && free && _screen != ScreenKind.Joining)
            {
                _intent = Intent.None;
                _target = null;
                _screen = ScreenFor(status);
            }
            _update.SetFree(free);
            if (_sessionOpen && free) RefreshMods();
            _sessionOpen = !free;
            ApplyOverlays(status, free);
        }
        finally
        {
            _applying = false;
        }
        Changed?.Invoke();
    }

    /// <summary>Which screen shows: the open view, else what the status asks for.</summary>
    private ScreenKind ScreenFor(AppStatus status)
    {
        if (_view == ScreenKind.Select && SelectedGame?.Profile.HasCharacterSelect != true) _view = null;
        return _view ?? Screens.For(status, _intent, _target, _tab);
    }

    /// <summary>What follows from the status and the screen: the lobby search runs while the Lobbies tab shows, a popup of another
    /// screen closes, the mini window follows the session.</summary>
    private void ApplyOverlays(AppStatus status, bool free)
    {
        var lobbiesShown = _screen == ScreenKind.Lobbies;
        if (_watchingLobbies != lobbiesShown)
        {
            _watchingLobbies = lobbiesShown;
            _app.WatchLobbies(lobbiesShown);
        }
        if (_popup != PopupKind.None && PopupScreen != _screen) _popup = PopupKind.None;
        if (_miniOpen && (free || status.Invite != null)) _miniOpen = false;
    }

    /// <summary>The screen the open popup belongs to: the options on the home screen, the lobby on the Lobbies tab.</summary>
    private ScreenKind PopupScreen => _popup == PopupKind.Options ? ScreenKind.Home : ScreenKind.Lobbies;

    // ---------------------------------------------------------------- mod states

    private void RefreshModsThrottled()
    {
        if (Environment.TickCount64 - _lastRefreshMs >= ModRefreshMinMs) RefreshMods();
    }

    /// <summary>Reads every game's mod state in the background (it hashes files) and shows it when done; a refusal that the new state
    /// resolves (the mod installed, the game found) goes.</summary>
    private void RefreshMods()
    {
        if (_refreshing)
        {
            _refreshAgain = true;
            return;
        }
        _refreshing = true;
        _lastRefreshMs = Environment.TickCount64;
        GameFolders.Invalidate();
        var profiles = _games.Select(game => game.Profile).ToList();
        Task.Run(() => profiles.Select(ReadStatus).OfType<(GameProfile, GameStatus, ArtDto)>().ToList()).ContinueWith(task =>
        {
            _refreshing = false;
            ShowStatuses(task.Result);
            if (!_refreshAgain) return;
            _refreshAgain = false;
            RefreshMods();
        }, _ui);
    }

    private static (GameProfile, GameStatus, ArtDto)? ReadStatus(GameProfile game)
    {
        try
        {
            return (game, GameStatus.Read(game), ArtOf(game));
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            Log.Info($"Could not read the {game.Name} mod state: {e.Message}");
            return null;
        }
    }

    private void ShowStatuses(IEnumerable<(GameProfile Game, GameStatus Status, ArtDto Art)> statuses)
    {
        var refusal = _app.Status.Refusal;
        var blocked = refusal?.GameId is { } id && !ModOf(id).CanHost;
        foreach (var (game, status, art) in statuses)
        {
            _games = _games.Select(entry => entry.Profile.Id == game.Id ? entry with { Status = status, Art = art } : entry).ToList();
            if (_modRuns.GetValueOrDefault(game.Id) is { Failed: true } failed && failed.Kind == ModRunKind.Install == status.ModInstalled) _modRuns.Remove(game.Id);
        }
        if (blocked && refusal!.GameId is { } gameId && SessionCards.Resolved(refusal, ModOf(gameId))) _app.ClearRefusal();
        Apply(_app.Status);
    }

    /// <summary>A mod action is under way (it runs off the UI thread; sessions and other mod actions wait for it).</summary>
    private bool ModRunning => _modRuns.Values.Any(run => run.Running);

    private GameStatus ModOf(string gameId) => _games.FirstOrDefault(game => game.Profile.Id == gameId)?.Status ?? GameStatus.Unknown;

    // ---------------------------------------------------------------- app events (any thread)

    private void OnStatusChanged(AppStatus status) => _post(() => Apply(status));

    private void OnAvatarLoaded(AvatarImage image) => _post(() =>
    {
        Avatars.Put(image.Id, image.Width, image.Height, image.Rgba);
        Changed?.Invoke();
    });

    private void OnInviteAccepted() => _post(() => InviteAccepted?.Invoke());
}
