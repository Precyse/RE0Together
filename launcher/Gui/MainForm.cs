namespace CoopLauncher.Gui;

/// <summary>The launcher window: game picker, host/join, lobby code, status and log. All session logic lives in App.</summary>
public sealed class MainForm : Form
{
    private const int WindowWidth = 560;
    private const int WindowHeight = 420;
    private const int OuterPadding = 8;
    private const int ColumnCount = 4;
    private const int FieldColumnSpan = 2;
    private const int StatusColumnSpan = 3;

    private readonly App _app;
    private readonly ComboBox _games = new() { DropDownStyle = ComboBoxStyle.DropDownList, Dock = DockStyle.Fill };
    private readonly TextBox _joinCode = new() { Dock = DockStyle.Fill };
    private readonly TextBox _lobbyCode = new() { ReadOnly = true, Dock = DockStyle.Fill };
    private readonly Button _host = ActionButton("Host");
    private readonly Button _join = ActionButton("Join");
    private readonly Button _copy = ActionButton("Copy");
    private readonly Button _invite = ActionButton("Invite");
    private readonly Button _leave = ActionButton("Leave");
    private readonly Label _lobbyLabel = Caption("Lobby");
    private readonly Label _status = new() { Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleLeft };
    private readonly TextBox _log = new() { Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Vertical, Dock = DockStyle.Fill };

    public MainForm(App app)
    {
        _app = app;
        Text = "Co-op launcher";
        ClientSize = new Size(WindowWidth, WindowHeight);
        Controls.Add(BuildLayout());
        LoadGames();
        Apply(app.Status);
        _host.Click += (_, _) => _app.Host(((GameProfile)_games.SelectedItem!).Id);
        _join.Click += (_, _) => JoinTypedCode();
        _copy.Click += (_, _) => Clipboard.SetText(_lobbyCode.Text);
        _invite.Click += (_, _) => _app.Invite();
        _leave.Click += (_, _) => _app.Leave();
        _app.StatusChanged += OnStatusChanged;
        Log.Written += OnLogWritten;
        FormClosed += (_, _) =>
        {
            _app.StatusChanged -= OnStatusChanged;
            Log.Written -= OnLogWritten;
        };
    }

    private static Button ActionButton(string text) => new() { Text = text, AutoSize = true, Dock = DockStyle.Fill };

    private static Label Caption(string text) => new() { Text = text, AutoSize = true, Anchor = AnchorStyles.Left, TextAlign = ContentAlignment.MiddleLeft };

    private TableLayoutPanel BuildLayout()
    {
        var grid = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = ColumnCount, Padding = new Padding(OuterPadding) };
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        for (var row = 0; row < 4; row++) grid.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        grid.RowStyles.Add(new RowStyle(SizeType.Percent, 100));

        grid.Controls.Add(Caption("Game"), 0, 0);
        grid.Controls.Add(_games, 1, 0);
        grid.SetColumnSpan(_games, FieldColumnSpan);
        grid.Controls.Add(_host, 3, 0);
        grid.Controls.Add(Caption("Lobby code"), 0, 1);
        grid.Controls.Add(_joinCode, 1, 1);
        grid.SetColumnSpan(_joinCode, FieldColumnSpan);
        grid.Controls.Add(_join, 3, 1);
        grid.Controls.Add(_lobbyLabel, 0, 2);
        grid.Controls.Add(_lobbyCode, 1, 2);
        grid.Controls.Add(_copy, 2, 2);
        grid.Controls.Add(_invite, 3, 2);
        grid.Controls.Add(_status, 0, 3);
        grid.SetColumnSpan(_status, StatusColumnSpan);
        grid.Controls.Add(_leave, 3, 3);
        grid.Controls.Add(_log, 0, 4);
        grid.SetColumnSpan(_log, ColumnCount);
        return grid;
    }

    private void LoadGames()
    {
        _games.DisplayMember = nameof(GameProfile.Name);
        foreach (var id in GameProfile.ListIds()) _games.Items.Add(GameProfile.Load(id));
        if (_games.Items.Count > 0) _games.SelectedIndex = 0;
    }

    private void JoinTypedCode()
    {
        if (ulong.TryParse(_joinCode.Text.Trim(), out var lobbyId)) _app.Join(lobbyId);
        else Log.Info("Invalid lobby code");
    }

    private void OnStatusChanged(AppStatus status) => OnUiThread(() => Apply(status));

    private void OnLogWritten(string line) => OnUiThread(() => _log.AppendText(line + Environment.NewLine));

    private void OnUiThread(Action action)
    {
        if (IsHandleCreated && !IsDisposed) BeginInvoke(action);
    }

    private void Apply(AppStatus status)
    {
        var idle = status.State == AppState.Idle;
        var hasLobby = status.LobbyId != 0;
        _status.Text = StatusText.Format(status);
        _host.Enabled = idle && _games.SelectedItem != null;
        _join.Enabled = idle;
        _leave.Enabled = !idle;
        _lobbyCode.Text = hasLobby ? status.LobbyId.ToString() : string.Empty;
        foreach (Control control in new Control[] { _lobbyLabel, _lobbyCode, _copy, _invite }) control.Visible = hasLobby;
    }
}
