using System.Runtime.InteropServices;

namespace CoopLauncher.Gui;

/// <summary>The launcher window, in the broadcast tool's operator look: top bar, game rail, the selected game's hero,
/// the session controls, the player seats and a log drawer. All session logic lives in App.</summary>
public sealed class MainForm : Form
{
    private const int WindowWidth = 840;
    private const int WindowHeight = 540;
    private const int LogHeight = 180;
    private const int SectionHeight = 92;
    private const int CaptionGap = 26;
    private const int HostWidth = 140;
    private const int ToolWidth = 84;
    private const int ModWidth = 100;
    private const int DwmUseImmersiveDarkMode = 20;

    private readonly App _app;
    private readonly TopBar _top = new();
    private readonly GameRail _rail = new();
    private readonly HeroBanner _hero = new();
    private readonly SectionPanel _session = new("Session") { Dock = DockStyle.Top, Height = SectionHeight };
    private readonly SectionPanel _playersSection = new("Players") { Dock = DockStyle.Fill, BottomLine = false };
    private readonly PlayerSlots _players = new() { Dock = DockStyle.Top };
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
    private readonly FlatButton _copy = new("Copy", ButtonKind.Normal);
    private readonly FlatButton _invite = new("Invite", ButtonKind.Normal);
    private bool _updateStaged;
    private readonly FlatButton _leave = new("Leave", ButtonKind.Ghost);
    private readonly TableLayoutPanel _lobbyRow;

    public MainForm(App app)
    {
        _app = app;
        Text = "Co-op";
        BackColor = Theme.Bg;
        ForeColor = Theme.Text;
        ClientSize = new Size(WindowWidth, WindowHeight);
        MinimumSize = Size;
        _idleRow = Row(new[] { (Control)_host, _mod, _joinCode, _join }, HostWidth, ModWidth, -1, ToolWidth);
        _lobbyRow = Row(new[] { (Control)_lobbyCode, _copy, _invite, _leave }, -1, ToolWidth, ToolWidth, ToolWidth);
        BuildLayout();
        LoadGames();
        RefreshMods();
        Apply(app.Status);

        _rail.SelectionChanged += _ => Apply(_app.Status);
        Shown += (_, _) => ReadReleaseInBackground();
        _host.Click += (_, _) => { if (_rail.Selected is { } game) _app.Host(game.Id); };
        _mod.Click += (_, _) => ChangeMod();
        _join.Click += (_, _) => JoinTypedCode();
        _joinCode.Input.KeyDown += (_, e) => { if (e.KeyCode == Keys.Enter) JoinTypedCode(); };
        _copy.Click += (_, _) => Clipboard.SetText(_lobbyCode.Text);
        _invite.Click += (_, _) => _app.Invite();
        _leave.Click += (_, _) => _app.Leave();
        _footer.Toggled += expanded => _log.Visible = expanded;
        _top.UpdateButton.Click += (_, _) => CheckForUpdate();
        _app.StatusChanged += OnStatusChanged;
        FormClosed += (_, _) =>
        {
            _app.StatusChanged -= OnStatusChanged;
            Log.Unsubscribe(OnLogWritten);
        };
    }

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

    /// <summary>A row of controls at control height; a width of -1 takes the remaining space.</summary>
    private static TableLayoutPanel Row(Control[] controls, params int[] widths)
    {
        var row = new TableLayoutPanel
        {
            Dock = DockStyle.Top, Height = Theme.ControlHeight, ColumnCount = controls.Length, RowCount = 1,
            BackColor = Theme.Bg, Margin = Padding.Empty, Padding = Padding.Empty,
        };
        for (var i = 0; i < controls.Length; i++)
        {
            row.ColumnStyles.Add(widths[i] < 0 ? new ColumnStyle(SizeType.Percent, 100) : new ColumnStyle(SizeType.Absolute, widths[i]));
            controls[i].Dock = DockStyle.Fill;
            controls[i].Margin = new Padding(i == 0 ? 0 : Theme.Gap, 0, 0, 0);
            row.Controls.Add(controls[i], i, 0);
        }
        return row;
    }

    private void BuildLayout()
    {
        _session.Controls.Add(_lobbyRow);
        _session.Controls.Add(_idleRow);
        _playersSection.Controls.Add(_players);

        var pane = new Panel { Dock = DockStyle.Fill, BackColor = Theme.Bg };
        pane.Controls.Add(_playersSection);
        pane.Controls.Add(_session);
        pane.Controls.Add(_hero);

        // Docking runs from the last control added, so the outer strips go in last.
        Controls.Add(pane);
        Controls.Add(_rail);
        Controls.Add(_log);
        Controls.Add(_footer);
        Controls.Add(_top);
    }

    private void LoadGames()
    {
        foreach (var id in GameProfile.ListIds())
        {
            var profile = GameProfile.Load(id);
            _rail.Add(profile, new SteamArt(profile.SteamAppId));
        }
    }

    /// <summary>Installs a newer release in the background without starting it: relaunching while this process holds a
    /// Steam session leaves the new copy with a broken one, so the player reopens the launcher.</summary>
    private void CheckForUpdate()
    {
        _top.UpdateButton.Enabled = false;
        Task.Run(Updater.Stage).ContinueWith(task =>
        {
            _updateStaged = task.Result;
            if (_updateStaged) Log.Info("Update installed. Close the launcher and open it again to use it");
            _top.UpdateButton.Enabled = !_updateStaged && _app.Status.State == AppState.Idle;
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
            var gameDir = SteamLibrary.FindGameDir(game.SteamAppId);
            _rail.SetStatus(game.Id, new GameStatus(ModInstaller.Status(game, gameDir), gameDir != null, BuildCheck.LocalBuild()));
        }
    }

    /// <summary>The mod button: Install, Update (re-copy) or Uninstall for the selected game.</summary>
    private void ChangeMod()
    {
        if (_rail.Selected is not { } game || SteamLibrary.FindGameDir(game.SteamAppId) is not { } gameDir) return;
        if (_rail.SelectedStatus.ModInstalled && !_rail.SelectedStatus.UpdateAvailable) ModInstaller.Uninstall(game, gameDir);
        else ModInstaller.Install(game, gameDir);
        RefreshMods();
        Apply(_app.Status);
    }

    private void JoinTypedCode()
    {
        if (ulong.TryParse(_joinCode.Input.Text.Trim(), out var lobbyId))
        {
            Log.Info($"Join requested for lobby {lobbyId}");
            _app.Join(lobbyId);
        }
        else
        {
            Log.Info("Invalid lobby code");
        }
    }

    private void OnStatusChanged(AppStatus status) => OnUiThread(() => Apply(status));

    private void OnLogWritten(string line) => OnUiThread(() => ShowLogLine(line));

    private void ShowLogLine(string line)
    {
        _log.AppendText(line + Environment.NewLine);
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
        var idle = status.State == AppState.Idle;
        var inLobby = status.LobbyId != 0;
        var running = status.State is AppState.GameRunning or AppState.PeerConnected;

        _top.Show(BuildLabel(), StatusText.Format(status), StatusText.Lamp(status.State));
        _rail.Enabled = idle;
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
        _host.Enabled = idle && mod.ModInstalled;
        _mod.Text = mod.Action.Label;
        _mod.Enabled = idle && mod.Action.Enabled;
        _mod.Invalidate();
        _join.Enabled = idle;
        _joinCode.Enabled = idle;
        _leave.Enabled = !idle;
        _top.UpdateButton.Enabled = idle && !_updateStaged;
    }

    private static string BuildLabel()
    {
        var build = BuildCheck.LocalBuild();
        return build > 0 ? $"build {build}" : "dev build";
    }

    /// <summary>A section of the right pane: a caption, its content under it, a separator under the section.</summary>
    private sealed class SectionPanel : Panel
    {
        private string _title;

        public SectionPanel(string title)
        {
            _title = title;
            BackColor = Theme.Bg;
            Padding = new Padding(Theme.SectionPadX, Theme.SectionPadY + CaptionGap, Theme.SectionPadX, 0);
            SetStyle(ControlStyles.ResizeRedraw | ControlStyles.OptimizedDoubleBuffer, true);
        }

        public bool BottomLine { get; init; } = true;

        public string Title
        {
            get => _title;
            set
            {
                if (_title == value) return;
                _title = value;
                Invalidate();
            }
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            base.OnPaint(e);
            Draw.Caption(e.Graphics, _title, Theme.Muted, new Point(Theme.SectionPadX, Theme.SectionPadY));
            if (BottomLine) Draw.HorizontalLine(e.Graphics, 0, Width, Height - 1);
        }
    }
}
