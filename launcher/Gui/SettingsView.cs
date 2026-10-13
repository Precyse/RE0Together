using System.ComponentModel;
using System.Diagnostics;

namespace CoopLauncher.Gui;

/// <summary>The settings pane: the launcher's own options, then one section per game (its folder with Browse / Reset to
/// the one Steam knows, and Uninstall). Scrolls when the sections outgrow the window. Every change is saved at once
/// through AppSettings.</summary>
internal sealed class SettingsView : Panel
{
    private readonly List<GameSection> _games = new();

    public SettingsView(IEnumerable<GameProfile> games)
    {
        var gameList = games.ToList();
        BackColor = Theme.Bg;
        Dock = DockStyle.Fill;
        AutoScroll = true;
        NativeTheme.DarkScrollbars(this);

        var sections = new List<SectionPanel> { LauncherSection(gameList) };
        foreach (var game in gameList)
        {
            var section = new GameSection(game);
            section.Changed += () => GameFolderChanged?.Invoke();
            section.UninstallClicked += () => UninstallRequested?.Invoke(game);
            _games.Add(section);
            sections.Add(section.Panel);
        }
        sections[^1].BottomLine = false;
        foreach (var section in sections) section.FitToContent();
        ControlRow.Stack(this, sections.ToArray<Control>());
        AutoScrollMinSize = new Size(0, sections.Sum(section => section.Height));
    }

    /// <summary>Raised after a game's folder setting changed.</summary>
    public event Action? GameFolderChanged;

    /// <summary>Raised when the player asks to remove a game's mod.</summary>
    public event Action<GameProfile>? UninstallRequested;

    /// <summary>Uninstall is offered for an installed mod while no session is open.</summary>
    public void ShowMods(Func<string, GameStatus> statusOf, bool free)
    {
        foreach (var game in _games) game.ShowMod(statusOf(game.Id), free);
    }

    private static SectionPanel LauncherSection(List<GameProfile> games)
    {
        var section = new SectionPanel("Launcher");
        var updates = new FlatToggle("Check for updates at start") { Checked = AppSettings.Current.CheckForUpdates };
        updates.Flipped += SetCheckForUpdates;
        var logs = new FlatButton("Logs folder", ButtonKind.Normal);
        logs.Click += (_, _) => OpenLogsFolder();
        var report = new FlatButton("Create report", ButtonKind.Normal);
        report.Click += (_, _) => CreateReport(games);
        var toolsRow = ControlRow.Create(new Control[] { logs, report, new Panel() }, Theme.ToolWidth, Theme.ToolWidth, -1);
        ControlRow.Stack(section, updates, ControlRow.Spacer(Theme.Gap), toolsRow);
        return section;
    }

    private static void SetCheckForUpdates(bool on)
    {
        AppSettings.Update(settings => settings with { CheckForUpdates = on });
        Log.Info(on ? "Checking for updates at start" : "Not checking for updates at start");
    }

    private static void OpenLogsFolder()
    {
        try
        {
            Directory.CreateDirectory(AppData.LogsDir);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            Log.Info($"Could not create the logs folder: {e.Message}");
            return;
        }
        StartExplorer($"\"{AppData.LogsDir}\"");
    }

    /// <summary>Zips the logs for sending and shows the zip in Explorer.</summary>
    private static void CreateReport(IEnumerable<GameProfile> games)
    {
        if (ReportBundle.Create(games) is { } path) StartExplorer($"/select,\"{path}\"");
    }

    private static void StartExplorer(string arguments)
    {
        try
        {
            Process.Start(new ProcessStartInfo("explorer.exe", arguments));
        }
        catch (Win32Exception e)
        {
            Log.Info($"Could not open Explorer: {e.Message}");
        }
    }

    /// <summary>One game's section: its folder, Browse, Reset and Uninstall.</summary>
    private sealed class GameSection
    {
        private readonly GameProfile _game;
        private readonly PathText _path = new();
        private readonly FlatButton _reset = new("Reset", ButtonKind.Ghost);
        private readonly FlatButton _uninstall = new("Uninstall", ButtonKind.Ghost);

        public GameSection(GameProfile game)
        {
            _game = game;
            Panel = new SectionPanel(game.Name);
            var browse = new FlatButton("Browse", ButtonKind.Normal);
            browse.Click += (_, _) => Browse();
            _reset.Click += (_, _) => Reset();
            _uninstall.Click += (_, _) => UninstallClicked?.Invoke();
            var row = ControlRow.Create(new Control[] { _path, browse, _reset, _uninstall }, -1, Theme.ToolWidth, Theme.ToolWidth, Theme.ToolWidth);
            ControlRow.Stack(Panel, row);
            Refresh();
        }

        public SectionPanel Panel { get; }

        public string Id => _game.Id;

        public event Action? Changed;

        public event Action? UninstallClicked;

        public void ShowMod(GameStatus status, bool free)
        {
            _uninstall.Enabled = free && status.GameFound && status.ModInstalled;
        }

        private void Browse()
        {
            using var dialog = new FolderBrowserDialog { InitialDirectory = GameFolders.Find(_game) ?? string.Empty };
            if (dialog.ShowDialog() != DialogResult.OK) return;
            if (!File.Exists(Path.Combine(dialog.SelectedPath, _game.Exe)))
            {
                Log.Info($"{_game.Exe} is not in {dialog.SelectedPath}");
                return;
            }
            Save(dialog.SelectedPath, $"{_game.Name} folder set to {dialog.SelectedPath}");
        }

        private void Reset() => Save(null, $"{_game.Name} folder reset to the one Steam knows");

        private void Save(string? folder, string message)
        {
            AppSettings.Update(settings => settings.WithGameFolder(_game.Id, folder));
            Log.Info(message);
            Refresh();
            Changed?.Invoke();
        }

        private void Refresh()
        {
            _path.Text = GameFolders.Find(_game) ?? string.Empty;
            _path.Invalidate();
            _reset.Enabled = AppSettings.Current.GameFolder(_game.Id) != null;
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
