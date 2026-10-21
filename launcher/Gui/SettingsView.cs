using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.InteropServices;

namespace CoopLauncher.Gui;

/// <summary>The settings pane: the launcher's own options, then one section per game (its folder with Browse / Reset to
/// the one Steam knows, Uninstall, the mod and game builds, the game's settings options and its saved sessions). Scrolls
/// when the sections outgrow the window; while a session is open every control that changes a game is locked. Every change
/// is saved at once through AppSettings.</summary>
internal sealed class SettingsView : Panel
{
    private readonly List<GameSection> _games = new();
    private readonly List<SectionPanel> _sections = new();

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
    /// for Uninstall).</summary>
    public void ShowMods(Func<string, GameStatus> statusOf, bool free, bool modsLocked)
    {
        foreach (var game in _games) game.Show(statusOf(game.Id), free, modsLocked);
    }

    /// <summary>Asks for a game's folder; true when the player chose one that holds the game and it was saved.</summary>
    public static bool BrowseFor(GameProfile game)
    {
        using var dialog = new FolderBrowserDialog { InitialDirectory = GameFolders.Find(game) ?? string.Empty };
        if (dialog.ShowDialog() != DialogResult.OK) return false;
        if (!File.Exists(Path.Combine(dialog.SelectedPath, game.Exe)))
        {
            Log.Info($"{game.Exe} is not in {dialog.SelectedPath}");
            return false;
        }
        AppSettings.Update(settings => settings.WithGameFolder(game.Id, dialog.SelectedPath));
        Log.Info($"{game.Name} folder set to {dialog.SelectedPath}");
        return true;
    }

    /// <summary>Zips the logs for sending, puts the zip's path on the clipboard and shows the zip in Explorer.</summary>
    public static void CreateReport(IEnumerable<GameProfile> games)
    {
        if (ReportBundle.Create(games) is not { } path) return;
        try
        {
            Clipboard.SetText(path);
        }
        catch (ExternalException e)
        {
            Log.Info($"Could not copy the report path: {e.Message}");
        }
        StartExplorer($"/select,\"{path}\"");
    }

    private void Refit()
    {
        foreach (var section in _sections) section.FitToContent();
        AutoScrollMinSize = new Size(0, _sections.Sum(section => section.Height));
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
            _archive.Click += (_, _) => OpenArchive();
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
            _line.Text = status.SettingsLine;
            _uninstall.Enabled = free && !modsLocked && status.GameFound && status.ModInstalled;
            _browse.Enabled = free;
            if (_hasSettingsOptions)
            {
                _options.Show(_game, GameFolders.Find(_game));
                _options.SetLocked(!free);
            }
            ShowFolder();
            ShowArchive();
            if (Panel.FittedHeight != Panel.Height) LayoutChanged?.Invoke();
        }

        private string? ArchiveFolder()
        {
            if (_game.SaveSync is not { } sync || GameFolders.Find(_game) is not { } gameDir) return null;
            return AdapterSettings.ArchiveRoot(Path.Combine(gameDir, sync.SessionDir));
        }

        private void ShowArchive()
        {
            var shown = ArchiveFolder() is { } folder && Directory.Exists(folder);
            _archiveRow.Height = shown ? Theme.Gap + Theme.ControlHeight : 0;
        }

        private void OpenArchive()
        {
            if (ArchiveFolder() is { } folder) StartExplorer($"\"{folder}\"");
        }

        private void Browse()
        {
            if (!BrowseFor(_game)) return;
            ShowFolder();
            Changed?.Invoke();
        }

        private void Reset()
        {
            AppSettings.Update(settings => settings.WithGameFolder(_game.Id, null));
            Log.Info($"{_game.Name} folder reset to the one Steam knows");
            ShowFolder();
            Changed?.Invoke();
        }

        private void ShowFolder()
        {
            _path.Text = GameFolders.Find(_game) ?? string.Empty;
            _path.Invalidate();
            _reset.Enabled = _free && AppSettings.Current.GameFolder(_game.Id) != null;
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
