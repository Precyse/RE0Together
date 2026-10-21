namespace CoopLauncher.Gui;

/// <summary>The settings pane: the launcher's own options, then one section per game (its folder with Browse / Reset to
/// the one Steam knows, Uninstall, the mod and game builds, the game's settings options and its saved sessions). Scrolls
/// when the sections outgrow the window; while a session is open every control that changes a game is locked. Every change
/// is saved at once through AppSettings.</summary>
internal sealed class SettingsView : Panel
{
    private const string VisibilityLabel = "Lobby visibility";
    private const string VisibilityFriends = "Friends";
    private const string VisibilityEveryone = "Everyone";
    private static readonly int VisibilityLabelWidth = Draw.Width(VisibilityLabel, Theme.Body) + Theme.Gap * 2;
    private static readonly int VisibilityWidth = Theme.Scale(160);

    private readonly List<GameSection> _games = new();
    private readonly List<SectionPanel> _sections = new();
    private readonly FlatSelect _visibility = new();

    public SettingsView(IEnumerable<GameProfile> games)
    {
        var gameList = games.ToList();
        BackColor = Theme.Bg;
        Dock = DockStyle.Fill;
        AutoScroll = true;
        NativeTheme.DarkScrollbars(this);

        _sections.Add(LauncherSection(gameList));
        foreach (var game in gameList)
        {
            var section = new GameSection(game);
            section.Changed += () => GameFolderChanged?.Invoke();
            section.UninstallClicked += () => UninstallRequested?.Invoke(game);
            section.LayoutChanged += Refit;
            _games.Add(section);
            _sections.Add(section.Panel);
        }
        _sections[^1].BottomLine = false;
        ControlRow.Stack(this, _sections.ToArray<Control>());
        Refit();
    }

    /// <summary>Raised after a game's folder setting changed.</summary>
    public event Action? GameFolderChanged;

    /// <summary>Raised when the player asks to remove a game's mod.</summary>
    public event Action<GameProfile>? UninstallRequested;

    /// <summary>Shows each game's mod and build line and locks what changes a game unless free (no session, no staged update
    /// for Uninstall); who may see a new lobby cannot change while a session is open either.</summary>
    public void ShowMods(Func<string, GameStatus> statusOf, bool free, bool modsLocked)
    {
        _visibility.Enabled = free;
        foreach (var game in _games) game.Show(statusOf(game.Id), free, modsLocked);
    }

    private void Refit()
    {
        foreach (var section in _sections) section.FitToContent();
        AutoScrollMinSize = new Size(0, _sections.Sum(section => section.Height));
    }

    private SectionPanel LauncherSection(List<GameProfile> games)
    {
        var section = new SectionPanel("Launcher");
        var updates = new FlatToggle("Check for updates at start") { Checked = AppSettings.Current.CheckForUpdates };
        updates.Flipped += LauncherActions.SetCheckForUpdates;
        var visibilityRow = VisibilityRow();
        var logs = new FlatButton("Logs folder", ButtonKind.Normal);
        logs.Click += (_, _) => LauncherActions.OpenLogsFolder();
        var report = new FlatButton("Create report", ButtonKind.Normal);
        report.Click += (_, _) => LauncherActions.CreateReport(games);
        var toolsRow = ControlRow.Create(new Control[] { logs, report, new Panel() }, Theme.ToolWidth, Theme.ToolWidth, -1);
        ControlRow.Stack(section, updates, ControlRow.Spacer(Theme.Gap), visibilityRow, ControlRow.Spacer(Theme.Gap), toolsRow);
        return section;
    }

    /// <summary>Lobby visibility: who can see a lobby this launcher opens (friends only, or everyone), saved at once.</summary>
    private TableLayoutPanel VisibilityRow()
    {
        var caption = new Label
        {
            Text = VisibilityLabel, AutoSize = false, Font = Theme.Body, ForeColor = Theme.Muted, BackColor = Theme.Bg, TextAlign = ContentAlignment.MiddleLeft,
        };
        _visibility.Items.Add(VisibilityFriends);
        _visibility.Items.Add(VisibilityEveryone);
        _visibility.SelectedIndex = (int)AppSettings.Current.LobbyVisibility;
        _visibility.SelectedIndexChanged += (_, _) =>
            AppSettings.Update(settings => settings.WithLobbyVisibility((LobbyVisibility)_visibility.SelectedIndex));
        return ControlRow.Create(new Control[] { caption, _visibility, new Panel() }, VisibilityLabelWidth, VisibilityWidth, -1);
    }

    /// <summary>One game's section: its folder (Browse, Reset, Uninstall), the mod and game line, the game's settings
    /// options and, once a session was saved, the Archive button.</summary>
    private sealed class GameSection
    {
        private readonly GameProfile _game;
        private readonly PathText _path = new();
        private readonly FlatButton _browse = new("Browse", ButtonKind.Normal);
        private readonly FlatButton _reset = new("Reset", ButtonKind.Ghost);
        private readonly FlatButton _uninstall = new("Uninstall", ButtonKind.Ghost);
        private readonly Label _line = new()
        {
            AutoSize = false, Dock = DockStyle.Top, Height = Theme.Small.Height + Theme.Gap, Font = Theme.Small, ForeColor = Theme.Muted,
            BackColor = Theme.Bg, TextAlign = ContentAlignment.MiddleLeft,
        };
        private readonly OptionsRows _options = new(settingsOptions: true);
        private readonly FlatButton _archive = new("Archive", ButtonKind.Normal) { Width = Theme.ToolWidth, Location = new Point(0, Theme.Gap) };
        private readonly Panel _archiveRow = new() { Dock = DockStyle.Top, BackColor = Theme.Bg, Height = 0 };
        private readonly bool _hasSettingsOptions;
        private bool _free = true;

        public GameSection(GameProfile game)
        {
            _game = game;
            Panel = new SectionPanel(game.Name);
            _browse.Click += (_, _) => Browse();
            _reset.Click += (_, _) => Reset();
            _uninstall.Click += (_, _) => UninstallClicked?.Invoke();
            _archive.Click += (_, _) => LauncherActions.OpenArchive(game);
            _archiveRow.Controls.Add(_archive);
            var row = ControlRow.Create(new Control[] { _path, _browse, _reset, _uninstall }, -1, Theme.ToolWidth, Theme.ToolWidth, Theme.ToolWidth);
            _hasSettingsOptions = (game.Options ?? []).Any(option => option.Settings);
            var rows = new List<Control> { row, _line };
            if (_hasSettingsOptions) rows.Add(_options);
            rows.Add(_archiveRow);
            ControlRow.Stack(Panel, rows.ToArray());
            ShowFolder();
        }

        public SectionPanel Panel { get; }

        public string Id => _game.Id;

        public event Action? Changed;

        public event Action? UninstallClicked;

        /// <summary>The section's height changed (its options were built, the archive button came or went).</summary>
        public event Action? LayoutChanged;

        public void Show(GameStatus status, bool free, bool modsLocked)
        {
            _free = free;
            _line.Text = SettingsModel.Line(status);
            _uninstall.Enabled = SettingsModel.UninstallEnabled(status, free, modsLocked);
            _browse.Enabled = SettingsModel.BrowseEnabled(free);
            if (_hasSettingsOptions)
            {
                _options.Show(_game, GameFolders.Find(_game));
                _options.SetLocked(!free);
            }
            ShowFolder();
            ShowArchive();
            if (Panel.FittedHeight != Panel.Height) LayoutChanged?.Invoke();
        }

        private void ShowArchive() => _archiveRow.Height = SettingsModel.ArchiveShown(_game) ? Theme.Gap + Theme.ControlHeight : 0;

        private void Browse()
        {
            if (!LauncherActions.BrowseFor(_game)) return;
            ShowFolder();
            Changed?.Invoke();
        }

        private void Reset()
        {
            LauncherActions.ResetFolder(_game);
            ShowFolder();
            Changed?.Invoke();
        }

        private void ShowFolder()
        {
            _path.Text = GameFolders.Find(_game) ?? string.Empty;
            _path.Invalidate();
            _reset.Enabled = SettingsModel.ResetEnabled(_game, _free);
        }
    }

    /// <summary>A folder path as plain text, shortened in the middle when it does not fit.</summary>
    private sealed class PathText : Control
    {
        public PathText()
        {
            BackColor = Theme.Bg;
            SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer
                     | ControlStyles.ResizeRedraw, true);
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            e.Graphics.Clear(BackColor);
            Draw.Path(e.Graphics, Text, Theme.Mono, Theme.Muted, ClientRectangle);
        }
    }
}
