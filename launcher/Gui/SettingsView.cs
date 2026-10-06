using System.ComponentModel;
using System.Diagnostics;

namespace CoopLauncher.Gui;

/// <summary>The settings pane: each game's folder (Browse / Reset to the one Steam knows) and the launcher's own options.
/// Every change is saved at once through AppSettings.</summary>
internal sealed class SettingsView : Panel
{
    private static readonly int NameHeight = Theme.Scale(20);
    private static readonly int BrowseWidth = Theme.Scale(84);
    private static readonly int ResetWidth = Theme.Scale(72);
    private static readonly int LogsButtonWidth = Theme.Scale(170);
    private static readonly int ReportButtonWidth = Theme.Scale(150);

    public SettingsView(IEnumerable<GameProfile> games)
    {
        var gameList = games.ToList();
        BackColor = Theme.Bg;
        Dock = DockStyle.Fill;
        var gamesSection = new SectionPanel("Games");
        var launcherSection = new SectionPanel("Launcher") { BottomLine = false, Dock = DockStyle.Fill };

        var parts = new List<Control>();
        foreach (var game in gameList)
        {
            var row = new GameFolderRow(game);
            row.Changed += () => GameFolderChanged?.Invoke();
            parts.Add(row.Name);
            parts.Add(row.Row);
            parts.Add(ControlRow.Spacer(Theme.Gap * 2));
        }
        ControlRow.Stack(gamesSection, parts.ToArray());
        gamesSection.FitToContent();

        var updates = new FlatToggle("Check for updates at start") { Checked = AppSettings.Current.CheckForUpdates };
        updates.Flipped += SetCheckForUpdates;
        var logs = new FlatButton("Open logs folder", ButtonKind.Normal);
        logs.Click += (_, _) => OpenLogsFolder();
        var report = new FlatButton("Create report", ButtonKind.Normal);
        report.Click += (_, _) => CreateReport(gameList);
        var logsRow = ControlRow.Create(new Control[] { logs, report, new Panel() }, LogsButtonWidth, ReportButtonWidth, -1);
        ControlRow.Stack(launcherSection, updates, ControlRow.Spacer(Theme.Gap), logsRow);

        Controls.Add(launcherSection);
        Controls.Add(gamesSection);
        gamesSection.Dock = DockStyle.Top;
    }

    /// <summary>Raised after a game's folder setting changed.</summary>
    public event Action? GameFolderChanged;

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

    /// <summary>One game's name, its folder, and the Browse and Reset buttons.</summary>
    private sealed class GameFolderRow
    {
        private readonly GameProfile _game;
        private readonly FieldBox _path = new();
        private readonly FlatButton _reset = new("Reset", ButtonKind.Ghost);

        public GameFolderRow(GameProfile game)
        {
            _game = game;
            Name = new Label
            {
                Text = game.Name, Font = Theme.Strong, ForeColor = Theme.Text, BackColor = Theme.Bg, Height = NameHeight,
                AutoSize = false, TextAlign = ContentAlignment.MiddleLeft, AutoEllipsis = true,
            };
            _path.Input.ReadOnly = true;
            var browse = new FlatButton("Browse", ButtonKind.Normal);
            browse.Click += (_, _) => Browse();
            _reset.Click += (_, _) => Reset();
            Row = ControlRow.Create(new Control[] { _path, browse, _reset }, -1, BrowseWidth, ResetWidth);
            Refresh();
        }

        public Label Name { get; }

        public TableLayoutPanel Row { get; }

        public event Action? Changed;

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
            _path.Input.Text = GameFolders.Find(_game) ?? string.Empty;
            _reset.Enabled = AppSettings.Current.GameFolder(_game.Id) != null;
        }
    }
}
