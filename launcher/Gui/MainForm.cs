using System.Runtime.InteropServices;

namespace CoopLauncher.Gui;

/// <summary>The launcher window, in the broadcast tool's operator look: top bar, game rail, the selected game's hero,
/// the session controls, the game's options, the player seats and a log drawer; the settings view takes the place of the
/// right pane.
/// All session logic lives in App.</summary>
public sealed class MainForm : Form
{
    private const string WindowTitle = "Co-op Launcher";
    private const string ConfirmCloseText = "Leave the session and close?";
    private const string JoinCodeLabel = "Join code";
    private const int WatchIntervalMs = 1000;
    private const int ModColumn = 1;
    private const int RelaunchColumn = 1;

    private static readonly int WindowWidth = Theme.Scale(840);
    private static readonly int WindowHeight = Theme.Scale(600);
    private static readonly int MinimumHeight = Theme.Scale(540);
    private static readonly int SectionHeight = Theme.Scale(92);
    private static readonly int JoinLabelWidth = Theme.Scale(80);

    private readonly App _app;
    private readonly TopBar _top = new();
    private readonly GameRail _rail = new();
    private readonly HeroBanner _hero = new();
    private readonly SectionPanel _session = new("Session") { Dock = DockStyle.Top, Height = SectionHeight };
    private readonly SectionPanel _optionsSection = new("Options") { Dock = DockStyle.Top };
    private readonly OptionsRows _options = new();
    private readonly SectionPanel _playersSection = new("Players") { Dock = DockStyle.Fill, BottomLine = false };
    private readonly PlayerSlots _players = new() { Dock = DockStyle.Top };
    private readonly Panel _pane = new() { Dock = DockStyle.Fill, BackColor = Theme.Bg, AutoScroll = true };
    private readonly Panel _content = new() { Dock = DockStyle.Fill, BackColor = Theme.Bg };
    private readonly LogDrawer _drawer = new();

    private readonly FlatButton _host = new("Host", ButtonKind.Primary);
    private readonly FlatButton _mod = new("Install", ButtonKind.Normal);
    private readonly Label _joinLabel = new()
    {
        Text = JoinCodeLabel, AutoSize = false, Font = Theme.Body, ForeColor = Theme.Muted, TextAlign = ContentAlignment.MiddleLeft,
    };
    private readonly FieldBox _joinCode = new();
    private readonly FlatButton _join = new("Join", ButtonKind.Ghost);
    private readonly TableLayoutPanel _idleRow;
    private readonly Label _lobbyCode = new()
    {
        AutoSize = false, Dock = DockStyle.Fill, Font = Theme.Code, ForeColor = Theme.Text, TextAlign = ContentAlignment.MiddleLeft,
    };
    private readonly FlatButton _relaunch = new("Relaunch", ButtonKind.Primary) { Visible = false };
    private readonly GameWatch _watch = new();
    private readonly System.Windows.Forms.Timer _watchTimer = new() { Interval = WatchIntervalMs };
    private readonly FlatButton _copy = new("Copy", ButtonKind.Normal);
    private readonly FlatButton _invite = new("Invite", ButtonKind.Normal);
    private bool _updateStaged;
    private readonly FlatButton _leave = new("Leave", ButtonKind.Ghost);
    private readonly TableLayoutPanel _lobbyRow;
    private SettingsView? _settings;

    public MainForm(App app)
    {
        _app = app;
        Text = WindowTitle;
        Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath);
        AutoScaleMode = AutoScaleMode.None;
        BackColor = Theme.Bg;
        ForeColor = Theme.Text;
        // The smallest window the pane scrolls in, then the default size.
        ClientSize = new Size(WindowWidth, MinimumHeight);
        MinimumSize = Size;
        ClientSize = new Size(WindowWidth, WindowHeight);
        WindowMemory.Restore(this);
        NativeTheme.DarkScrollbars(_pane);
        _idleRow = ControlRow.Create(new[] { (Control)_host, _mod, _joinLabel, _joinCode, _join },
            Theme.PrimaryWidth, Theme.ToolWidth, JoinLabelWidth, -1, Theme.ToolWidth);
        _lobbyRow = ControlRow.Create(new[] { (Control)_lobbyCode, _relaunch, _copy, _invite, _leave },
            -1, 0, Theme.ToolWidth, Theme.ToolWidth, Theme.ToolWidth);
        LoadGames();
        BuildLayout();
        RefreshMods();
        Apply(app.Status);

        _rail.SelectionChanged += _ =>
        {
            ShowSettings(false);
            Apply(_app.Status);
        };
        Shown += (_, _) =>
        {
            if (AppSettings.Current.CheckForUpdates) ReadReleaseInBackground();
        };
        _options.Changed += () => Apply(_app.Status);
        _host.Click += (_, _) => { if (_rail.Selected is { } game) _app.Host(game.Id); };
        _mod.Click += (_, _) => InstallMod();
        _join.Click += (_, _) => JoinTypedCode();
        _joinCode.Input.KeyDown += (_, e) => { if (e.KeyCode == Keys.Enter) JoinTypedCode(); };
        _relaunch.Click += (_, _) => RelaunchGame();
        _watchTimer.Tick += (_, _) => WatchGame();
        _watchTimer.Start();
        _copy.Click += (_, _) => CopyLobbyCode();
        _invite.Click += (_, _) => _app.Invite();
        _leave.Click += (_, _) => _app.Leave();
        _top.UpdateButton.Click += (_, _) => CheckForUpdate();
        _top.SettingsButton.Click += (_, _) => ShowSettings(!_settings!.Visible);
        _app.StatusChanged += OnStatusChanged;
        FormClosing += (_, e) => OnClosing(e);
        FormClosed += (_, _) =>
        {
            _watchTimer.Dispose();
            _app.StatusChanged -= OnStatusChanged;
            Log.Unsubscribe(OnLogWritten);
        };
    }

    /// <summary>Closing the window with a session open asks first; closing ends the session (GuiHost stops the app loop,
    /// which leaves the lobby and shuts Steam down) and leaves the game running.</summary>
    private void OnClosing(FormClosingEventArgs e)
    {
        var inSession = _app.Status.State is not (AppState.Idle or AppState.Offline);
        if (inSession && e.CloseReason == CloseReason.UserClosing)
        {
            var answer = MessageBox.Show(this, ConfirmCloseText, WindowTitle, MessageBoxButtons.YesNo, MessageBoxIcon.Question, MessageBoxDefaultButton.Button2);
            if (answer != DialogResult.Yes)
            {
                e.Cancel = true;
                return;
            }
        }
        if (inSession) Log.Info("Leaving the session, the window was closed");
        WindowMemory.Save(this);
    }

    /// <summary>A second start of the launcher asks this window to come forward (called from any thread).</summary>
    public void BringForward() => OnUiThread(() =>
    {
        if (WindowState == FormWindowState.Minimized) WindowState = FormWindowState.Normal;
        Activate();
    });

    protected override void OnHandleCreated(EventArgs e)
    {
        base.OnHandleCreated(e);
        NativeTheme.DarkTitleBar(this);
        // Subscribed only once the handle exists: lines logged before it (start-up, a fatal error of the app loop) come
        // from the log's history instead of being dropped.
        foreach (var line in Log.Subscribe(OnLogWritten)) _drawer.ShowLine(line);
    }

    private void BuildLayout()
    {
        _session.Controls.Add(_lobbyRow);
        _session.Controls.Add(_idleRow);
        _optionsSection.Controls.Add(_options);
        _playersSection.Controls.Add(_players);

        _pane.Controls.Add(_playersSection);
        _pane.Controls.Add(_optionsSection);
        _pane.Controls.Add(_session);
        _pane.Controls.Add(_hero);

        _settings = new SettingsView(_rail.Games) { Visible = false };
        _settings.GameFolderChanged += () =>
        {
            RefreshMods();
            Apply(_app.Status);
        };

        _settings.UninstallRequested += UninstallMod;

        // The pane and the settings swap inside one panel: a control first shown after start-up is re-ordered among
        // its siblings, which must never let it dock against the rail. Docking runs from the last control added, so the
        // outer strips go in last.
        _content.Controls.Add(_settings);
        _content.Controls.Add(_pane);
        Controls.Add(_content);
        Controls.Add(_rail);
        Controls.Add(_drawer);
        Controls.Add(_top);
    }

    private void ShowSettings(bool open)
    {
        _settings!.Visible = open;
        _pane.Visible = !open;
        _top.ShowSettingsOpen(open);
    }

    private void LoadGames()
    {
        try
        {
            foreach (var id in GameProfile.ListIds())
            {
                var profile = GameProfile.Load(id);
                _rail.Add(profile, new SteamArt(profile.SteamAppId));
            }
        }
        catch (Exception e) when (e is IOException or System.Text.Json.JsonException or TypeInitializationException)
        {
            Log.Info($"Game profiles unreadable: {e.Message}");
        }
    }

    /// <summary>Installs a newer release in the background without starting it (the one forced read of the release):
    /// relaunching while this process holds a Steam session leaves the new copy with a broken one, so the player reopens
    /// the launcher. Otherwise the mod states are re-read.</summary>
    private void CheckForUpdate()
    {
        _top.UpdateButton.Enabled = false;
        Task.Run(Updater.Stage).ContinueWith(task =>
        {
            _updateStaged = task.Result;
            if (_updateStaged) Log.Info("Update installed. Close the launcher and open it again to use it");
            ShowLauncherUpdate();
            RefreshMods();
            Apply(_app.Status);
        }, TaskScheduler.FromCurrentSynchronizationContext());
    }

    /// <summary>The startup read of the release (cached; the updater's read already filled it when packaged).</summary>
    private void ReadReleaseInBackground() =>
        Task.Run(() => ReleaseFeed.Read()).ContinueWith(_ => ShowLauncherUpdate(), TaskScheduler.FromCurrentSynchronizationContext());

    private void ShowLauncherUpdate()
    {
        var latest = ReleaseFeed.Read()?.Build;
        _top.ShowAvailable(latest is { } build && build > BuildCheck.LocalBuild() ? $"Build {build} available" : null);
    }

    /// <summary>Reads every game's mod state: installed or not, and whether the package holds a newer copy.</summary>
    private void RefreshMods()
    {
        foreach (var game in _rail.Games.ToList())
        {
            var gameDir = GameFolders.Find(game);
            var gameBuild = GameBuilds.Installed(game, gameDir);
            _rail.SetStatus(game.Id, new GameStatus(
                ModInstaller.Status(game, gameDir), gameDir != null, BuildCheck.LocalBuild(), gameBuild, GameBuilds.IsSupported(game, gameBuild)));
        }
    }

    /// <summary>Once a second: notices the game of an open session exiting, or running without the adapter link.</summary>
    private void WatchGame()
    {
        var status = _app.Status;
        var game = _rail.Games.FirstOrDefault(g => g.Id == status.GameId);
        if (!_watch.Update(status, game, Environment.TickCount64)) return;
        if (game != null && _watch.Condition == GameCondition.Exited) Log.Info($"{game.Name} exited");
        if (game != null && _watch.Condition == GameCondition.Unlinked) Log.Info($"{game.Name} is running without the co-op link (started outside the launcher?)");
        Apply(status);
    }

    /// <summary>Starts the game again for the open session; the session folder is kept, so it catches up as it joins.</summary>
    private void RelaunchGame()
    {
        var game = _rail.Games.FirstOrDefault(g => g.Id == _app.Status.GameId);
        if (game != null) GameLauncher.Launch(game, GameFolders.Find(game));
    }

    /// <summary>The session row's mod button: Install, or Update (re-copy), for the selected game.</summary>
    private void InstallMod()
    {
        if (_rail.Selected is not { } game || GameFolders.Find(game) is not { } gameDir) return;
        ModInstaller.Install(game, gameDir);
        RefreshMods();
        Apply(_app.Status);
    }

    /// <summary>A game section's Uninstall in the settings view.</summary>
    private void UninstallMod(GameProfile game)
    {
        if (GameFolders.Find(game) is not { } gameDir) return;
        ModInstaller.Uninstall(game, gameDir);
        RefreshMods();
        Apply(_app.Status);
    }

    private void JoinTypedCode()
    {
        if (ulong.TryParse(_joinCode.Input.Text.Trim(), out var lobbyId) && lobbyId != 0)
        {
            Log.Info($"Join requested for lobby {lobbyId}");
            _app.Join(lobbyId);
        }
        else
        {
            Log.Info("Invalid lobby code");
        }
    }

    private void CopyLobbyCode()
    {
        try
        {
            Clipboard.SetText(_lobbyCode.Text);
            Log.Info("Lobby code copied");
        }
        catch (ExternalException e)
        {
            Log.Info($"Could not copy the lobby code: {e.Message}");
        }
    }

    private void OnStatusChanged(AppStatus status) => OnUiThread(() => Apply(status));

    private void OnLogWritten(string line) => OnUiThread(() => _drawer.ShowLine(line));

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

    private void Apply(AppStatus status)
    {
        if (status.GameId is { } lobbyGame) _rail.SelectGame(lobbyGame);
        var idle = status.State == AppState.Idle;
        var free = idle || status.State == AppState.Offline;
        var inLobby = status.LobbyId != 0;
        var running = status.State is AppState.GameRunning or AppState.PeerConnected;

        var exited = _watch.Condition == GameCondition.Exited;
        _top.Show(BuildLabel(), StatusText.Format(status, _watch.Condition), StatusText.Lamp(status.State, _watch.Condition));
        _relaunch.Visible = exited;
        _lobbyRow.ColumnStyles[RelaunchColumn].Width = exited ? Theme.ToolWidth : 0;
        _rail.Enabled = free;
        _rail.SetRunning(running ? _rail.Selected?.Id : null);
        if (_rail.Selected is { } game)
        {
            var gameDir = GameFolders.Find(game);
            _hero.Show(game, _rail.SelectedArt!);
            _options.Show(game, gameDir);
            _options.SetLocked(!free);
            _optionsSection.FitToContent();
            var seats = status.SeatCharacters.Count > 0
                ? status.SeatCharacters
                : GameOptions.SeatCharacters(game, GameOptions.HostValues(game, gameDir));
            _players.Show(game, status.Players, status.RttMs, seats);
        }

        _session.Title = inLobby ? "Lobby" : "Session";
        _idleRow.Visible = !inLobby;
        _lobbyRow.Visible = inLobby;
        _lobbyCode.Text = inLobby ? status.LobbyId.ToString() : string.Empty;
        var mod = _rail.SelectedStatus;
        _host.Enabled = idle && mod.CanHost;
        var offerMod = !mod.ModInstalled || mod.UpdateAvailable;
        _mod.Visible = offerMod;
        _idleRow.ColumnStyles[ModColumn].Width = offerMod ? Theme.ToolWidth : 0;
        _mod.Text = mod.Action.Label;
        _mod.Enabled = free && mod.Action.Enabled;
        _mod.Invalidate();
        _settings!.ShowMods(_rail.StatusOf, free);
        FitPane();
        _join.Enabled = idle;
        _joinCode.Enabled = idle;
        _leave.Enabled = !free;
        _top.UpdateButton.Enabled = free && !_updateStaged;
    }

    private static string BuildLabel()
    {
        var build = BuildCheck.LocalBuild();
        return build > 0 ? $"build {build}" : "dev build";
    }

    /// <summary>The pane scrolls once its sections and the seats no longer fit the window.</summary>
    private void FitPane()
    {
        _pane.AutoScrollMinSize = new Size(0, _hero.Height + _session.Height + _optionsSection.Height + _playersSection.FittedHeight);
    }
}
