using System.Runtime.InteropServices;

namespace CoopLauncher.Gui;

/// <summary>The launcher window, in the broadcast tool's operator look: top bar, game rail, the selected game's hero,
/// the session controls, the player seats and a log drawer; the settings view takes the place of the right pane.
/// All session logic lives in App.</summary>
public sealed class MainForm : Form
{
    private const string WindowTitle = "Co-op Launcher";
    private const string ConfirmCloseText = "Leave the session and close?";
    private const int LogMaxChars = 60_000;
    private const int LogKeepChars = 40_000;
    private const int DwmUseImmersiveDarkMode = 20;
    private const int WatchIntervalMs = 1000;
    private const int RelaunchColumn = 1;

    private static readonly int WindowWidth = Theme.Scale(840);
    private static readonly int WindowHeight = Theme.Scale(540);
    private static readonly int LogHeight = Theme.Scale(180);
    private static readonly int SectionHeight = Theme.Scale(92);
    private static readonly int HostWidth = Theme.Scale(140);
    private static readonly int ToolWidth = Theme.Scale(84);
    private static readonly int ModWidth = Theme.Scale(100);
    private static readonly int RelaunchWidth = Theme.Scale(110);

    private readonly App _app;
    private readonly TopBar _top = new();
    private readonly GameRail _rail = new();
    private readonly HeroBanner _hero = new();
    private readonly SectionPanel _session = new("Session") { Dock = DockStyle.Top, Height = SectionHeight };
    private readonly SectionPanel _playersSection = new("Players") { Dock = DockStyle.Fill, BottomLine = false };
    private readonly PlayerSlots _players = new() { Dock = DockStyle.Top };
    private readonly Panel _pane = new() { Dock = DockStyle.Fill, BackColor = Theme.Bg };
    private readonly LogFooter _footer = new();
    private readonly TextBox _log = new()
    {
        Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Vertical, Dock = DockStyle.Bottom, Height = LogHeight,
        BorderStyle = BorderStyle.None, BackColor = Theme.Bg2, ForeColor = Theme.Muted, Font = Theme.Mono, Visible = false,
    };

    private readonly FlatButton _host = new("Host", ButtonKind.Primary);
    private readonly FlatButton _mod = new("Install", ButtonKind.Normal);
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
        ClientSize = new Size(WindowWidth, WindowHeight);
        MinimumSize = Size;
        WindowMemory.Restore(this);
        _idleRow = ControlRow.Create(new[] { (Control)_host, _mod, _joinCode, _join }, HostWidth, ModWidth, -1, ToolWidth);
        _lobbyRow = ControlRow.Create(new[] { (Control)_lobbyCode, _relaunch, _copy, _invite, _leave }, -1, 0, ToolWidth, ToolWidth, ToolWidth);
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
        _host.Click += (_, _) => { if (_rail.Selected is { } game) _app.Host(game.Id); };
        _mod.Click += (_, _) => ChangeMod();
        _join.Click += (_, _) => JoinTypedCode();
        _joinCode.Input.KeyDown += (_, e) => { if (e.KeyCode == Keys.Enter) JoinTypedCode(); };
        _relaunch.Click += (_, _) => RelaunchGame();
        _watchTimer.Tick += (_, _) => WatchGame();
        _watchTimer.Start();
        _copy.Click += (_, _) => CopyLobbyCode();
        _invite.Click += (_, _) => _app.Invite();
        _leave.Click += (_, _) => _app.Leave();
        _footer.Toggled += expanded => _log.Visible = expanded;
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
        var dark = 1;
        DwmSetWindowAttribute(Handle, DwmUseImmersiveDarkMode, ref dark, sizeof(int));
        // Subscribed only once the handle exists: lines logged before it (start-up, a fatal error of the app loop) come
        // from the log's history instead of being dropped.
        foreach (var line in Log.Subscribe(OnLogWritten)) ShowLogLine(line);
    }

    [DllImport("dwmapi.dll")]
    private static extern int DwmSetWindowAttribute(IntPtr hwnd, int attribute, ref int value, int size);

    private void BuildLayout()
    {
        _session.Controls.Add(_lobbyRow);
        _session.Controls.Add(_idleRow);
        _playersSection.Controls.Add(_players);

        _pane.Controls.Add(_playersSection);
        _pane.Controls.Add(_session);
        _pane.Controls.Add(_hero);

        _settings = new SettingsView(_rail.Games) { Visible = false };
        _settings.GameFolderChanged += () =>
        {
            RefreshMods();
            Apply(_app.Status);
        };

        // Docking runs from the last control added, so the outer strips go in last.
        Controls.Add(_settings);
        Controls.Add(_pane);
        Controls.Add(_rail);
        Controls.Add(_log);
        Controls.Add(_footer);
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

    /// <summary>The mod button: Install, Update (re-copy) or Uninstall for the selected game.</summary>
    private void ChangeMod()
    {
        if (_rail.Selected is not { } game || GameFolders.Find(game) is not { } gameDir) return;
        if (_rail.SelectedStatus.ModInstalled && !_rail.SelectedStatus.UpdateAvailable) ModInstaller.Uninstall(game, gameDir);
        else ModInstaller.Install(game, gameDir);
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

    private void OnLogWritten(string line) => OnUiThread(() => ShowLogLine(line));

    private void ShowLogLine(string line)
    {
        _log.AppendText(line + Environment.NewLine);
        if (_log.TextLength > LogMaxChars)
        {
            _log.Text = _log.Text[^LogKeepChars..];
            _log.SelectionStart = _log.TextLength;
            _log.ScrollToCaret();
        }
        _footer.ShowLine(line);
    }

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
        _lobbyRow.ColumnStyles[RelaunchColumn].Width = exited ? RelaunchWidth : 0;
        _rail.Enabled = free;
        _rail.SetRunning(running ? _rail.Selected?.Id : null);
        if (_rail.Selected is { } game)
        {
            _hero.Show(game, _rail.SelectedArt!);
            _players.Show(game, status.Players, status.RttMs);
        }

        _session.Title = inLobby ? "Lobby" : "Session";
        _idleRow.Visible = !inLobby;
        _lobbyRow.Visible = inLobby;
        _lobbyCode.Text = inLobby ? status.LobbyId.ToString() : string.Empty;
        var mod = _rail.SelectedStatus;
        _host.Enabled = idle && mod.CanHost;
        _mod.Text = mod.Action.Label;
        _mod.Enabled = free && mod.Action.Enabled;
        _mod.Invalidate();
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
}
