namespace CoopLauncher.Gui;

/// <summary>The launcher window, in the broadcast tool's operator look and without Windows' title bar: the top bar (which
/// is the title bar), game rail, the selected game's hero, the Session, Options and Players sections and a log drawer; the
/// settings view takes the place of the right pane. It only composes: what each part shows is worked out in
/// <see cref="SessionCards"/> and <see cref="GameStatus"/>, session logic lives in App, the update path in
/// <see cref="UpdateFlow"/>.</summary>
public sealed class MainForm : ChromelessForm
{
    private const string WindowTitle = "Co-op Launcher";
    private const string ConfirmCloseText = "Leave the session and close?";
    private const string ConfirmLeaveLabel = "Leave and close";
    private const string ConfirmStayLabel = "Stay";
    private const string DevBuildLabel = "Dev build";
    private const int WatchIntervalMs = 1000;
    private const long ModRefreshMinMs = 2000;

    private static readonly int WindowWidth = Theme.Scale(840);
    private static readonly int WindowHeight = Theme.Scale(600);
    private static readonly int MinimumHeight = Theme.Scale(540);
    private static readonly int ScrollbarGutter = SystemInformation.VerticalScrollBarWidth;

    private readonly App _app;
    private readonly AvatarStore _avatars = new();
    private readonly TopBar _top = new();
    private readonly GameRail _rail = new();
    private readonly HeroBanner _hero = new();
    private readonly SessionSection _session;
    private readonly SectionPanel _optionsSection = new("Options") { Dock = DockStyle.Top };
    private readonly OptionsRows _options = new();
    private readonly SectionPanel _playersSection = new("Players") { Dock = DockStyle.Top, BottomLine = false };
    private readonly PlayerCards _players;
    private readonly Panel _pane = new() { Dock = DockStyle.Fill, BackColor = Theme.Bg, AutoScroll = true };
    private readonly Panel _content = new() { Dock = DockStyle.Fill, BackColor = Theme.Bg };
    private readonly LogDrawer _drawer = new();
    private readonly GameWatch _watch = new();
    private readonly System.Windows.Forms.Timer _watchTimer = new() { Interval = WatchIntervalMs };
    private readonly UpdateFlow _update = new();
    private readonly string _buildLabel = BuildLabel();
    private readonly TaskScheduler _ui = TaskScheduler.FromCurrentSynchronizationContext();
    private readonly SettingsView _settings;
    private bool _settingsOpen;
    private bool _applying;
    private bool _sessionOpen;
    private bool _refreshing;
    private bool _refreshAgain;
    private long _lastRefreshMs;
    private bool? _orderedForFree;

    public MainForm(App app)
    {
        _app = app;
        _session = new SessionSection(_avatars);
        _players = new PlayerCards(_avatars) { Dock = DockStyle.Top };
        Text = WindowTitle;
        Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath);
        AutoScaleMode = AutoScaleMode.None;
        BackColor = Theme.Bg;
        ForeColor = Theme.Text;
        KeyPreview = true;
        MinimumSize = SizeFromClientSize(new Size(WindowWidth, MinimumHeight));
        Size = SizeFromClientSize(new Size(WindowWidth, WindowHeight));
        WindowMemory.Restore(this);
        NativeTheme.DarkScrollbars(_pane);
        LoadGames();
        _settings = new SettingsView(_rail.Games) { Visible = false };
        BuildLayout();
        RefreshMods();
        Apply(app.Status);
        WireEvents();
    }

    protected override int CaptionHeight => _top.Height;

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
        if (events.Count > 0) _drawer.ShowEvent(events[^1]);
    }

    protected override void OnKeyDown(KeyEventArgs e)
    {
        if (e.KeyCode == Keys.Escape && _settingsOpen && ActiveControl is not FlatSelect { DroppedDown: true })
        {
            ShowSettings(false);
            e.Handled = true;
        }
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
        _rail.SelectionChanged += OnRailSelected;
        _options.Changed += () => Apply(_app.Status);
        _top.SettingsButton.Click += (_, _) => ShowSettings(!_settingsOpen);
        _top.UpdateButton.Click += (_, _) => _update.Click();
        _update.Changed += () =>
        {
            if (!_applying) Apply(_app.Status);
        };
        _update.RestartRequested += RestartNow;
        _settings.GameFolderChanged += () => RefreshMods();
        _settings.UninstallRequested += UninstallMod;

        _session.HostClicked += HostOrLaunch;
        _session.ModClicked += () =>
        {
            if (_rail.Selected is { } game) InstallMod(game);
        };
        _session.JoinRequested += JoinTypedCode;
        _session.FriendJoinClicked += _app.Join;
        _session.RelaunchClicked += RelaunchGame;
        _session.CopyClicked += CopyLobbyCode;
        _session.InviteClicked += _app.Invite;
        _session.LeaveClicked += _app.Leave;
        _session.NoticeActionClicked += RunNoticeFix;
        _session.NoticeReportClicked += () => SettingsView.CreateReport(_rail.Games);
        _session.LayoutChanged += () => Fit(_session.Panel);
        foreach (var section in new[] { _session.Panel, _optionsSection, _playersSection }) section.Resize += (_, _) => FitPane();
        _pane.Resize += (_, _) => FitPane();

        Shown += (_, _) => _update.Check(automatic: AppSettings.Current.CheckForUpdates);
        Activated += (_, _) => RefreshModsThrottled();
        _watchTimer.Tick += (_, _) => WatchGame();
        _watchTimer.Start();
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
            _avatars.Dispose();
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
        _optionsSection.Controls.Add(_options);
        _playersSection.Controls.Add(_players);
        _pane.Controls.AddRange([_playersSection, _optionsSection, _session.Panel, _hero]);

        // The pane and the settings swap inside one panel: a control first shown after start-up is re-ordered among
        // its siblings, which must never let it dock against the rail. Docking runs from the last control added, so the
        // outer strips go in last.
        _content.Controls.Add(_settings);
        _content.Controls.Add(_pane);
        Controls.Add(_content);
        Controls.Add(_rail);
        Controls.Add(_drawer);
        Controls.Add(_top);
        _rail.TabIndex = 0;
        _content.TabIndex = 1;
        _top.TabIndex = 2;
        _drawer.TabIndex = 3;
    }

    private void ShowSettings(bool open)
    {
        if (_settingsOpen == open) return;
        _settingsOpen = open;
        _top.ShowSettingsOpen(open);
        if (open)
        {
            _settings.Visible = true;
            _pane.Visible = false;
            _settings.PerformLayout();
            MotionOverlay.Play(_settings, Durations.PanelOpen, Easing.Out, Recipes.PanelIn, Theme.Bg);
            return;
        }
        _pane.Visible = true;
        _pane.PerformLayout();
        var under = MotionOverlay.Capture(_pane);
        MotionOverlay.Play(_settings, Durations.PanelClose, Easing.Out, Recipes.PanelOut, Theme.Bg, under);
        _settings.Visible = false;
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
                _rail.Add(profile, new SteamArt(profile.SteamAppId));
            }
            catch (Exception e) when (e is IOException or System.Text.Json.JsonException or InvalidDataException or TypeInitializationException)
            {
                Log.Info($"Game profile {id} unreadable: {e.Message}");
            }
        }
        if (AppSettings.Current.LastGame is { } last) _rail.SelectGame(last);
    }

    /// <summary>The player picked a game: a refusal of the earlier one goes, the choice is remembered.</summary>
    private void OnRailSelected(GameProfile game)
    {
        if (_applying) return;
        _app.ClearRefusal();
        AppSettings.Update(settings => settings.WithLastGame(game.Id));
        ShowSettings(false);
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
        var games = _rail.Games.ToList();
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
        var blocked = refusal?.GameId is { } id && !_rail.StatusOf(id).CanHost;
        foreach (var (game, status) in statuses) _rail.SetStatus(game.Id, status);
        if (blocked && refusal!.GameId is { } gameId && SessionCards.Resolved(refusal, _rail.StatusOf(gameId))) _app.ClearRefusal();
        Apply(_app.Status);
    }

    // ---------------------------------------------------------------- actions

    /// <summary>Once a second: notices the game of an open session exiting, or running without the adapter link.</summary>
    private void WatchGame()
    {
        var status = _app.Status;
        var game = _rail.Games.FirstOrDefault(g => g.Id == status.GameId);
        if (!_watch.Update(status, game, Environment.TickCount64)) return;
        if (game != null && _watch.Condition == GameCondition.Exited) Log.Event($"{game.Name} exited");
        if (game != null && _watch.Condition == GameCondition.Unlinked) Log.Info($"{game.Name} is running without the co-op link (started outside the launcher?)");
        Apply(status);
    }

    /// <summary>Starts the game again for the open session; the session folder is kept, so it catches up as it joins.</summary>
    private void RelaunchGame()
    {
        var game = _rail.Games.FirstOrDefault(g => g.Id == _app.Status.GameId);
        if (game != null) GameLauncher.Launch(game, GameFolders.Find(game));
    }

    /// <summary>The session row's main button: opens a lobby, or for a game that matches its players itself starts the game.</summary>
    private void HostOrLaunch()
    {
        if (_rail.Selected is not { } game) return;
        if (!game.NativeSession) _app.Host(game.Id);
        else if (GameFolders.Find(game) is { } gameDir) GameLauncher.LaunchWithOptions(game, gameDir, GameOptions.Current(game, gameDir));
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

    private void JoinTypedCode(string text)
    {
        if (!LobbyCode.TryParse(text, out var lobbyId))
        {
            _session.RefuseCode();
            return;
        }
        Log.Info($"Join requested for lobby {lobbyId}");
        _app.Join(lobbyId);
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

    /// <summary>The notice row's fix button, for the refusal or the invite on show.</summary>
    private void RunNoticeFix()
    {
        var status = _app.Status;
        var game = _rail.Games.FirstOrDefault(g => g.Id == status.Refusal?.GameId) ?? _rail.Selected;
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
                _app.Join(refusal.LobbyId);
                break;
            case NoticeFix.Switch:
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

    private void OnEventLogged(string message) => OnUiThread(() => _drawer.ShowEvent(message));

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

    // ---------------------------------------------------------------- showing a status

    private void Apply(AppStatus status)
    {
        _applying = true;
        try
        {
            if (status.GameId is { } lobbyGame) _rail.SelectGame(lobbyGame);
            var free = status.State is AppState.Idle or AppState.Offline;
            var condition = _watch.Condition;
            _top.Show(_buildLabel, StatusText.Format(status, condition), StatusText.Lamp(status, condition));
            _rail.Enabled = free;
            _rail.SetRunning(status.State is AppState.GameRunning or AppState.PeerConnected ? status.GameId : null);
            if (_rail.Selected is { } game) ApplyGame(status, game, free, condition);
            _settings.ShowMods(_rail.StatusOf, free, Updater.Staged);
            _update.SetFree(free);
            _top.ShowUpdate(_update.Label, _update.Enabled);
            if (_sessionOpen && free) RefreshMods();
            _sessionOpen = !free;
            FitPane();
        }
        finally
        {
            _applying = false;
        }
    }

    private void ApplyGame(AppStatus status, GameProfile game, bool free, GameCondition condition)
    {
        var gameDir = GameFolders.Find(game);
        var hostValues = GameOptions.HostValues(game, gameDir);
        _hero.Show(game, _rail.SelectedArt!);
        _options.Show(game, gameDir, free ? null : status.HostOptions);
        _options.SetLocked(!free);
        _session.Show(SessionCards.View(status, game, _rail.SelectedStatus, Updater.Staged,
            condition is GameCondition.Exited or GameCondition.NotStarted, _update.NewerBuild, _rail.Games));
        if (game.NativeSession) _players.Show([]);
        else if (status.State is not (AppState.Rejoining or AppState.WaitingForHost))
            _players.Show(SessionCards.Seats(status, status.SeatCharacters.Count > 0 ? status.SeatCharacters : GameOptions.SeatCharacters(game, hostValues),
                status.Seats > 0 ? status.Seats : GameOptions.Seats(game, hostValues)));
        OrderSections(free);
        Fit(_optionsSection);
        Fit(_playersSection);
    }

    /// <summary>Idle: Session, Options, Players. In a session: Session, Players, Options. Docking stacks the control with the
    /// highest child index at the top, and Tab follows the same order.</summary>
    private void OrderSections(bool free)
    {
        if (_orderedForFree == free) return;
        _orderedForFree = free;
        Control[] topToBottom = free
            ? [_hero, _session.Panel, _optionsSection, _playersSection]
            : [_hero, _session.Panel, _playersSection, _optionsSection];
        for (var i = 0; i < topToBottom.Length; i++)
        {
            _pane.Controls.SetChildIndex(topToBottom[i], topToBottom.Length - 1 - i);
            topToBottom[i].TabIndex = i;
        }
    }

    /// <summary>Sizes a section to its content, tweening the height (card-resize) when it has to change.</summary>
    private static void Fit(SectionPanel section)
    {
        if (section.FittedHeight != section.Height) section.FitToContent(animate: true);
    }

    /// <summary>The pane scrolls once its sections no longer fit the window; the scrollbar's gutter stays reserved while it is
    /// hidden, so nothing shifts between games.</summary>
    private void FitPane()
    {
        var content = _hero.Height + _session.Panel.Height + _optionsSection.Height + _playersSection.Height;
        _pane.AutoScrollMinSize = new Size(0, content);
        var gutter = content > _pane.ClientSize.Height ? 0 : ScrollbarGutter;
        if (_pane.Padding.Right != gutter) _pane.Padding = new Padding(0, 0, gutter, 0);
    }
}
