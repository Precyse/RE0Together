namespace CoopLauncher.Gui;

/// <summary>The launcher window, in the broadcast tool's operator look and without Windows' title bar: the top bar (which is the
/// title bar) over one screen at a time, and a log footer on the home and lobbies screens. The screens are the home (game rail,
/// hero, friends), the lobby browser, joining, hosting and the lobby, and the settings and character select views; popups and
/// the mini window sit on top. This part holds the window's parts, their wiring and the actions; <c>MainForm.Screens.cs</c> works out
/// which screen shows and fills it. What each part shows is worked out in <see cref="SessionCards"/>, <see cref="Screens"/>,
/// <see cref="SessionScreens"/> and <see cref="GameStatus"/>, session logic lives in App, the update path in <see cref="UpdateFlow"/>.</summary>
public sealed partial class MainForm : ChromelessForm
{
    private const string WindowTitle = "Co-op Launcher";
    private const string ConfirmCloseText = "Leave the session and close?";
    private const string ConfirmLeaveLabel = "Leave and close";
    private const string ConfirmStayLabel = "Stay";
    private const string DevBuildLabel = "Dev build";
    private const int WatchIntervalMs = 1000;
    private const int KeptEvents = 50;
    private const long ModRefreshMinMs = 2000;

    private static readonly int WindowWidth = Theme.Scale(840);
    private static readonly int WindowHeight = Theme.Scale(600);
    private static readonly int MinimumHeight = Theme.Scale(540);

    private readonly App _app;
    private readonly AvatarStore _avatars = new();
    private readonly TopBar _top = new();
    private readonly ScreenHost _host = new();
    private readonly HomeScreen _home;
    private readonly LobbiesScreen _lobbies;
    private readonly JoiningScreen _joining;
    private readonly SessionScreen _session;
    private readonly SettingsView _settings;
    private readonly CharacterSelectView _select;
    private readonly ModalLayer _modal = new() { Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right };
    private readonly LogDrawer _drawer = new();
    private readonly OutfitArtStore _art;
    private readonly GameWatch _watch = new();
    private readonly System.Windows.Forms.Timer _watchTimer = new() { Interval = WatchIntervalMs };
    private readonly UpdateFlow _update = new();
    private readonly string _buildLabel = BuildLabel();
    private readonly TaskScheduler _ui = TaskScheduler.FromCurrentSynchronizationContext();
    private readonly List<string> _events = new();
    private readonly List<GameProfile> _games = new();

    private MiniWindow? _mini;
    private Tab _tab;
    private Intent _intent;
    private JoinTarget? _target;
    private ScreenKind? _view;
    private IReadOnlyList<LobbyRowModel> _lobbyRows = [];
    private bool _watchingLobbies;
    private bool _applying;
    private bool _sessionOpen;
    private bool _refreshing;
    private bool _refreshAgain;
    private long _lastRefreshMs;

    public MainForm(App app)
    {
        _app = app;
        Text = WindowTitle;
        Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath);
        AutoScaleMode = AutoScaleMode.None;
        BackColor = Theme.Bg;
        ForeColor = Theme.Text;
        KeyPreview = true;
        MinimumSize = SizeFromClientSize(new Size(WindowWidth, MinimumHeight));
        Size = SizeFromClientSize(new Size(WindowWidth, WindowHeight));
        WindowMemory.Restore(this);
        _home = new HomeScreen(_avatars);
        LoadGames();
        _games.AddRange(Rail.Games);
        _settings = new SettingsView(_games);
        _art = new OutfitArtStore(_ui);
        _select = new CharacterSelectView(_art);
        _lobbies = new LobbiesScreen(_avatars, Rail.ArtOf);
        _lobbies.SetGames(_games);
        _joining = new JoiningScreen(_avatars);
        _session = new SessionScreen(_avatars, _art);
        BuildLayout();
        RefreshMods();
        Apply(app.Status);
        WireEvents();
    }

    protected override int CaptionHeight => _top.Height;

    private GameRail Rail => _home.Rail;

    /// <summary>A second start of the launcher asks this window to come forward (called from any thread).</summary>
    public void BringForward() => OnUiThread(Raise);

    private void Raise()
    {
        if (WindowState == FormWindowState.Minimized) WindowState = FormWindowState.Normal;
        Activate();
    }

    protected override void OnHandleCreated(EventArgs e)
    {
        base.OnHandleCreated(e);
        NativeTheme.DarkFrame(this);
        // Subscribed only once the handle exists: lines logged before it (start-up, a fatal error of the app loop) come
        // from the log's history instead of being dropped.
        foreach (var line in Log.Subscribe(OnLogWritten)) _drawer.ShowLine(line);
        var events = Log.SubscribeEvents(OnEventLogged);
        _events.AddRange(events.TakeLast(KeptEvents));
        if (events.Count > 0) _drawer.ShowEvent(events[^1]);
    }

    protected override void OnKeyDown(KeyEventArgs e)
    {
        if (e.KeyCode == Keys.Escape && ActiveControl is not FlatSelect { DroppedDown: true } && CloseOnEscape()) e.Handled = true;
        base.OnKeyDown(e);
    }

    /// <summary>The launcher's build for the top bar, worked out once.</summary>
    private static string BuildLabel()
    {
        var build = BuildCheck.LocalBuild();
        return build > BuildInfo.DevBuild ? $"Build {build}" : DevBuildLabel;
    }

    private void WireEvents()
    {
        Rail.SelectionChanged += OnRailSelected;
        _select.Changed += () => Apply(_app.Status);
        _top.Tabs.Picked += index =>
        {
            _tab = (Tab)index;
            Apply(_app.Status);
        };
        _top.SettingsButton.Click += (_, _) => ShowView(ScreenKind.Settings);
        _top.BackButton.Click += (_, _) => ShowView(null);
        _top.UpdateButton.Click += (_, _) => _update.Click();
        _update.Changed += () =>
        {
            if (!_applying) Apply(_app.Status);
        };
        _update.RestartRequested += RestartNow;
        _settings.GameFolderChanged += () => RefreshMods();
        _settings.UninstallRequested += UninstallMod;
        WireHome();
        WireLobbies();
        WireSession();
        _modal.Dismissed += CloseModal;

        Shown += (_, _) => _update.Check(automatic: AppSettings.Current.CheckForUpdates);
        Activated += (_, _) => RefreshModsThrottled();
        _watchTimer.Tick += (_, _) => OnSecond();
        _app.AvatarLoaded += OnAvatarLoaded;
        _app.StatusChanged += OnStatusChanged;
        _app.InviteAccepted += OnInviteAccepted;
        _app.ResendAvatars();
        FormClosing += (_, e) => OnClosing(e);
        FormClosed += (_, _) =>
        {
            _watchTimer.Dispose();
            _app.StatusChanged -= OnStatusChanged;
            _app.AvatarLoaded -= OnAvatarLoaded;
            _app.InviteAccepted -= OnInviteAccepted;
            Log.Unsubscribe(OnLogWritten);
            Log.UnsubscribeEvents(OnEventLogged);
            _mini?.Dispose();
            _avatars.Dispose();
            _art.Dispose();
        };
    }

    private void WireHome()
    {
        _home.HostClicked += HostOrLaunch;
        _home.ModClicked += () =>
        {
            if (Rail.Selected is { } game) InstallMod(game);
        };
        _home.JoinRequested += JoinTypedCode;
        _home.FriendJoinClicked += JoinFriend;
        _home.OptionsClicked += ToggleOptions;
        _home.CharactersClicked += () => ShowView(ScreenKind.Select);
        _home.NoticeActionClicked += RunNoticeFix;
        _home.NoticeReportClicked += () => SettingsView.CreateReport(_games);
    }

    private void WireLobbies()
    {
        _lobbies.Changed += () => Apply(_app.Status);
        _lobbies.ActionClicked += RunLobbyAction;
        _lobbies.Opened += OpenLobbyPopup;
    }

    private void WireSession()
    {
        _joining.CancelClicked += CancelSession;
        _joining.FixClicked += RunNoticeFix;
        _joining.StartAgainClicked += RelaunchGame;
        _session.StartClicked += _app.StartGame;
        _session.LeaveClicked += _app.Leave;
        _session.CopyClicked += CopyLobbyCode;
        _session.MiniClicked += StartMini;
        _session.RelaunchClicked += RelaunchGame;
        _session.NoticeActionClicked += RunNoticeFix;
        _session.NoticeReportClicked += () => SettingsView.CreateReport(_games);
        _session.SeatClicked += label =>
        {
            _select.FocusCharacter(label);
            ShowView(ScreenKind.Select);
        };
    }

    /// <summary>Closing the window with a session open asks first; closing ends the session (GuiHost stops the app loop,
    /// which leaves the lobby and shuts Steam down) and leaves the game running.</summary>
    private void OnClosing(FormClosingEventArgs e)
    {
        var inSession = _app.Status.State is not (AppState.Idle or AppState.Offline);
        if (inSession && e.CloseReason == CloseReason.UserClosing && !ConfirmDialog.Ask(this, ConfirmCloseText, ConfirmLeaveLabel, ConfirmStayLabel))
        {
            e.Cancel = true;
            return;
        }
        if (inSession) Log.Info("Leaving the session, the window was closed");
        WindowMemory.Save(this);
    }

    private void BuildLayout()
    {
        _host.Add(_home, ScreenKind.Home);
        _host.Add(_lobbies, ScreenKind.Lobbies);
        _host.Add(_joining, ScreenKind.Joining);
        _host.Add(_session, ScreenKind.Hosting, ScreenKind.Lobby);
        _host.Add(_settings, ScreenKind.Settings);
        _host.Add(_select, ScreenKind.Select);
        // Docking runs from the last control added, so the outer strips go in last.
        Controls.Add(_modal);
        Controls.Add(_host);
        Controls.Add(_drawer);
        Controls.Add(_top);
        _host.TabIndex = 0;
        _top.TabIndex = 1;
        _drawer.TabIndex = 2;
    }

    /// <summary>Fills the rail with the games whose profile can be read; one unreadable profile is logged and skipped. The
    /// game of the last session is selected first.</summary>
    private void LoadGames()
    {
        foreach (var id in GameProfile.ListIds())
        {
            try
            {
                var profile = GameProfile.Load(id);
                Rail.Add(profile, new SteamArt(profile.SteamAppId));
            }
            catch (Exception e) when (e is IOException or System.Text.Json.JsonException or InvalidDataException or TypeInitializationException)
            {
                Log.Info($"Game profile {id} unreadable: {e.Message}");
            }
        }
        if (AppSettings.Current.LastGame is { } last) Rail.SelectGame(last);
    }

    /// <summary>The player picked a game: an open view closes, a refusal of the earlier game goes, the choice is remembered.</summary>
    private void OnRailSelected(GameProfile game)
    {
        if (_applying) return;
        _view = null;
        _app.ClearRefusal();
        AppSettings.Update(settings => settings.WithLastGame(game.Id));
        Apply(_app.Status);
    }

    // ---------------------------------------------------------------- mod states

    private void RefreshModsThrottled()
    {
        if (Environment.TickCount64 - _lastRefreshMs >= ModRefreshMinMs) RefreshMods();
    }

    /// <summary>Reads every game's mod state in the background (it hashes files) and shows it when done; a refusal that the
    /// new state resolves (the mod installed, the game found) goes.</summary>
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
        var games = _games.ToList();
        Task.Run(() => games.Select(ReadStatus).OfType<(GameProfile, GameStatus)>().ToList()).ContinueWith(task =>
        {
            _refreshing = false;
            ShowStatuses(task.Result);
            if (!_refreshAgain) return;
            _refreshAgain = false;
            RefreshMods();
        }, _ui);
    }

    private static (GameProfile, GameStatus)? ReadStatus(GameProfile game)
    {
        try
        {
            return (game, GameStatus.Read(game));
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            Log.Info($"Could not read the {game.Name} mod state: {e.Message}");
            return null;
        }
    }

    private void ShowStatuses(IEnumerable<(GameProfile Game, GameStatus Status)> statuses)
    {
        var refusal = _app.Status.Refusal;
        var blocked = refusal?.GameId is { } id && !Rail.StatusOf(id).CanHost;
        foreach (var (game, status) in statuses) Rail.SetStatus(game.Id, status);
        if (blocked && refusal!.GameId is { } gameId && SessionCards.Resolved(refusal, Rail.StatusOf(gameId))) _app.ClearRefusal();
        Apply(_app.Status);
    }

    // ---------------------------------------------------------------- actions

    /// <summary>Once a second while a session is open or a lobby popup shows: notices the game of an open session exiting, or
    /// running without the adapter link, and refreshes what counts time.</summary>
    private void OnSecond()
    {
        var status = _app.Status;
        var game = GameOf(status);
        var changed = _watch.Update(status, game, Environment.TickCount64);
        if (changed && game != null && _watch.Condition == GameCondition.Exited) Log.Event($"{game.Name} exited");
        if (changed && game != null && _watch.Condition == GameCondition.Unlinked) Log.Info($"{game.Name} is running without the co-op link (started outside the launcher?)");
        if (_modal.Panel is LobbyPopup popup) popup.Tick(DateTimeOffset.UtcNow.ToUnixTimeSeconds());
        if (changed || status.State is not (AppState.Idle or AppState.Offline)) Apply(status);
    }

    /// <summary>Starts the game again for the open session; the session folder is kept, so it catches up as it joins.</summary>
    private void RelaunchGame()
    {
        var game = GameOf(_app.Status);
        if (game != null) GameLauncher.Launch(game, GameFolders.Find(game));
    }

    /// <summary>The home hero's main button: opens a lobby, or for a game that matches its players itself starts the game.</summary>
    private void HostOrLaunch()
    {
        if (Rail.Selected is not { } game) return;
        if (game.NativeSession)
        {
            if (GameFolders.Find(game) is { } gameDir) GameLauncher.LaunchWithOptions(game, gameDir, GameOptions.Current(game, gameDir));
            return;
        }
        _intent = Intent.Host;
        _target = null;
        _app.Host(game.Id);
        Apply(_app.Status);
    }

    /// <summary>Asks to join a lobby: the joining screen shows at once for this target.</summary>
    private void Join(JoinTarget target)
    {
        _intent = Intent.Join;
        _target = target;
        Log.Info($"Join requested for lobby {target.LobbyId}");
        _app.Join(target.LobbyId);
        Apply(_app.Status);
    }

    private void JoinTypedCode(string text)
    {
        if (!LobbyCode.TryParse(text, out var lobbyId))
        {
            _home.RefuseCode();
            return;
        }
        Join(new JoinTarget(lobbyId, string.Empty, 0));
    }

    private void JoinFriend(ulong lobbyId)
    {
        var friend = _app.Status.FriendLobbies.FirstOrDefault(candidate => candidate.LobbyId == lobbyId);
        Join(new JoinTarget(lobbyId, friend?.Name ?? string.Empty, friend?.FriendId ?? 0));
    }

    /// <summary>Cancel on the joining screen: the session is left, a refusal goes and the window returns to its tab.</summary>
    private void CancelSession()
    {
        _app.Leave();
        _app.ClearRefusal();
        _intent = Intent.None;
        _target = null;
        Apply(_app.Status);
    }

    /// <summary>Installs, updates or repairs a game's mod; true when it was installed.</summary>
    private bool InstallMod(GameProfile game)
    {
        if (GameFolders.Find(game) is not { } gameDir) return false;
        var installed = ModInstaller.Install(game, gameDir);
        RefreshMods();
        return installed;
    }

    /// <summary>A game section's Uninstall in the settings view.</summary>
    private void UninstallMod(GameProfile game)
    {
        if (GameFolders.Find(game) is not { } gameDir) return;
        ModInstaller.Uninstall(game, gameDir);
        RefreshMods();
    }

    private void CopyLobbyCode()
    {
        try
        {
            Clipboard.SetText(_app.Status.LobbyId.ToString());
            Log.Info("Lobby code copied");
        }
        catch (System.Runtime.InteropServices.ExternalException e)
        {
            Log.Info($"Could not copy the lobby code: {e.Message}");
        }
    }

    /// <summary>The fix button of a notice or of the joining screen, for the refusal or the invite on show.</summary>
    private void RunNoticeFix()
    {
        var status = _app.Status;
        var game = _games.FirstOrDefault(g => g.Id == status.Refusal?.GameId) ?? Rail.Selected;
        switch (SessionCards.FixFor(status, _update.NewerBuild))
        {
            case NoticeFix.InstallMod or NoticeFix.UpdateMod when game != null:
                if (InstallMod(game)) _app.ClearRefusal();
                break;
            case NoticeFix.UpdateLauncher:
                _update.Click();
                break;
            case NoticeFix.Browse when game != null:
                if (SettingsView.BrowseFor(game)) RefreshMods();
                break;
            case NoticeFix.JoinAgain when status.Refusal is { } refusal:
                Join(_target is { } target && target.LobbyId == refusal.LobbyId ? target : new JoinTarget(refusal.LobbyId, string.Empty, 0));
                break;
            case NoticeFix.Switch when status.Invite is { } invite:
                _intent = Intent.Join;
                _target = new JoinTarget(invite.LobbyId, invite.FromName, invite.FromId);
                _app.SwitchToInvite();
                break;
        }
    }

    /// <summary>The Restart button: the staged build starts once this window has closed (Program relaunches it).</summary>
    private void RestartNow()
    {
        Updater.RestartRequested = true;
        Close();
    }

    /// <summary>An invite was accepted: the window comes forward, and when Windows keeps it behind its taskbar button flashes.</summary>
    private void OnInviteAccepted() => OnUiThread(() =>
    {
        Raise();
        if (ActiveForm != this) WindowFlash.Start(this);
    });

    private void OnAvatarLoaded(AvatarImage image) => OnUiThread(() => _avatars.Put(image.Id, image.Width, image.Height, image.Rgba));

    private void OnStatusChanged(AppStatus status) => OnUiThread(() => Apply(status));

    private void OnLogWritten(string line) => OnUiThread(() => _drawer.ShowLine(line));

    private void OnEventLogged(string message) => OnUiThread(() =>
    {
        _events.Add(message);
        if (_events.Count > KeptEvents) _events.RemoveAt(0);
        _drawer.ShowEvent(message);
        Apply(_app.Status);
    });

    private void OnUiThread(Action action)
    {
        try
        {
            if (IsHandleCreated && !IsDisposed) BeginInvoke(action);
        }
        catch (Exception e) when (e is InvalidOperationException or ObjectDisposedException)
        {
            // The window closed between the check and the call; the log or status line has nowhere to go.
        }
    }
}
