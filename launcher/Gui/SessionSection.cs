namespace CoopLauncher.Gui;

/// <summary>The Session section of the main pane. Idle: Host, the mod button when the mod needs something, the Lobby code
/// field and Join, and under them the friends hosting. In a lobby: Relaunch when the game is gone, the lobby code, Copy,
/// Invite and Leave. Under either, the notice row. The caller hands it a <see cref="SessionView"/>; it reports what the
/// player clicked and, when its content changes height, asks to be refitted.</summary>
internal sealed class SessionSection
{
    private const string HostLabel = "HOST";
    private const string LaunchLabel = "LAUNCH";
    private const string RelaunchLabel = "RELAUNCH";
    private const string CodeLabel = "Lobby code";
    private const int CodeDigits = 20;
    private const char CodeSample = '0';

    private static readonly int CodeLabelWidth = Theme.Scale(80);
    private static readonly int CodeWidth = Draw.Width(new string(CodeSample, CodeDigits), Theme.Code);

    private readonly Panel _stack = new() { Dock = DockStyle.Top, BackColor = Theme.Bg };
    private readonly FlatButton _host = new(HostLabel, ButtonKind.Primary);
    private readonly FlatButton _mod = new("Install", ButtonKind.Normal);
    private readonly FieldBox _codeField = new();
    private readonly FlatButton _join = new("Join", ButtonKind.Ghost);
    private readonly TableLayoutPanel _joinRow;
    private readonly TableLayoutPanel _idleRow;
    private readonly FriendRows _friends;
    private readonly Panel _lobbyRow = new() { BackColor = Theme.Bg };
    private readonly FlatButton _relaunch = new(RelaunchLabel, ButtonKind.Primary);
    private readonly Label _code = new()
    {
        AutoSize = false, Font = Theme.Code, ForeColor = Theme.Text, TextAlign = ContentAlignment.MiddleLeft, BackColor = Theme.Bg,
    };
    private readonly FlatButton _copy = new("Copy", ButtonKind.Normal) { Width = Theme.ToolWidth };
    private readonly FlatButton _invite = new("Invite", ButtonKind.Normal) { Width = Theme.ToolWidth };
    private readonly FlatButton _leave = new("Leave", ButtonKind.Ghost) { Width = Theme.ToolWidth };
    private readonly NoticeRow _notice = new();
    private SessionView? _view;
    private bool _noticeSpace;
    private int _arrangedWidth;

    public SessionSection(AvatarStore avatars)
    {
        Panel = new SectionPanel("Session") { Dock = DockStyle.Top };
        _friends = new FriendRows(avatars);
        var codeCaption = new Label
        {
            Text = CodeLabel, AutoSize = false, Font = Theme.Body, ForeColor = Theme.Muted, TextAlign = ContentAlignment.MiddleLeft,
        };
        _joinRow = ControlRow.Create(new Control[] { codeCaption, _codeField, _join }, CodeLabelWidth, -1, Theme.ToolWidth);
        _idleRow = ControlRow.Create(new[] { (Control)_host, _mod, _joinRow }, Theme.PrimaryWidth, Theme.ToolWidth, -1);
        _lobbyRow.Controls.AddRange([_relaunch, _code, _copy, _invite, _leave]);
        _stack.Controls.AddRange([_idleRow, _friends, _lobbyRow, _notice]);
        for (var i = 0; i < _stack.Controls.Count; i++) _stack.Controls[i].TabIndex = i;
        Panel.Controls.Add(_stack);

        _host.Click += (_, _) => HostClicked?.Invoke();
        _mod.Click += (_, _) => ModClicked?.Invoke();
        _join.Click += (_, _) => JoinRequested?.Invoke(_codeField.Input.Text);
        _codeField.Input.KeyDown += (_, e) =>
        {
            if (e.KeyCode != Keys.Enter) return;
            e.SuppressKeyPress = true;
            JoinRequested?.Invoke(_codeField.Input.Text);
        };
        _friends.JoinClicked += lobbyId => FriendJoinClicked?.Invoke(lobbyId);
        _relaunch.Click += (_, _) => RelaunchClicked?.Invoke();
        _copy.Click += (_, _) => CopyClicked?.Invoke();
        _invite.Click += (_, _) => InviteClicked?.Invoke();
        _leave.Click += (_, _) => LeaveClicked?.Invoke();
        _notice.ActionClicked += () => NoticeActionClicked?.Invoke();
        _notice.ReportClicked += () => NoticeReportClicked?.Invoke();
        _notice.Collapsed += () =>
        {
            _noticeSpace = false;
            Arrange();
            LayoutChanged?.Invoke();
        };
        _lobbyRow.Resize += (_, _) => ArrangeLobbyRow();
        _stack.SizeChanged += (_, _) =>
        {
            if (_stack.Width == _arrangedWidth) return;
            Arrange();
            LayoutChanged?.Invoke();
        };
    }

    public SectionPanel Panel { get; }

    public event Action? HostClicked;
    public event Action? ModClicked;

    /// <summary>Join was asked for with this text in the Lobby code field.</summary>
    public event Action<string>? JoinRequested;
    public event Action<ulong>? FriendJoinClicked;
    public event Action? RelaunchClicked;
    public event Action? CopyClicked;
    public event Action? InviteClicked;
    public event Action? LeaveClicked;
    public event Action? NoticeActionClicked;
    public event Action? NoticeReportClicked;

    /// <summary>The section's content changed height: fit the section to it.</summary>
    public event Action? LayoutChanged;

    /// <summary>The typed lobby code was refused: the field shakes and turns amber.</summary>
    public void RefuseCode() => _codeField.Refuse();

    public void Show(SessionView view)
    {
        _view = view;
        Panel.Title = view.Title;
        _host.Text = view.Native ? LaunchLabel : HostLabel;
        _host.Enabled = view.HostEnabled;
        _mod.Visible = view.Mod != null;
        _mod.Text = view.Mod?.Label ?? string.Empty;
        _mod.Enabled = view.ModEnabled;
        _idleRow.ColumnStyles[1].Width = view.Mod != null ? Theme.ToolWidth : 0;
        _joinRow.Visible = !view.Native;
        _join.Enabled = view.JoinEnabled;
        _codeField.Enabled = view.CodeEnabled;
        _friends.Show(view.Friends);
        _idleRow.Visible = !view.InLobby;
        _friends.Visible = !view.InLobby && view.Friends.Count > 0;
        _lobbyRow.Visible = view.InLobby;
        _relaunch.Visible = view.Relaunch;
        _code.Text = view.Code;
        _copy.Enabled = view.CopyEnabled;
        _invite.Enabled = view.InviteEnabled;
        _leave.Enabled = view.LeaveEnabled;
        _noticeSpace |= view.Notice != null;
        Arrange();
        _notice.Show(view.Notice);
        LayoutChanged?.Invoke();
    }

    private bool RelaunchWraps => _view is { Relaunch: true } && _stack.Width < RelaunchRowWidth;

    /// <summary>The width one lobby row needs with Relaunch in front of the code: below it Relaunch takes a row of its own.</summary>
    private static int RelaunchRowWidth => Theme.PrimaryWidth + Theme.Gap + CodeWidth + 3 * (Theme.Gap + Theme.ToolWidth);

    /// <summary>Stacks the visible rows from the top and sizes the stack to them.</summary>
    private void Arrange()
    {
        if (_view == null) return;
        var width = _arrangedWidth = _stack.Width;
        var y = 0;
        if (_view.InLobby)
        {
            var height = RelaunchWraps ? 2 * Theme.ControlHeight + Theme.Gap : Theme.ControlHeight;
            _lobbyRow.SetBounds(0, y, width, height);
            ArrangeLobbyRow();
            y += height;
        }
        else
        {
            _idleRow.SetBounds(0, y, width, Theme.ControlHeight);
            y += Theme.ControlHeight;
            if (_view.Friends.Count > 0)
            {
                y += Theme.Gap;
                _friends.SetBounds(0, y, width, FriendRows.HeightFor(_view.Friends.Count));
                y += _friends.Height;
            }
        }
        if (_noticeSpace)
        {
            y += Theme.Gap;
            _notice.SetBounds(0, y, width, Theme.ControlHeight);
            y += Theme.ControlHeight;
        }
        _stack.Height = y;
    }

    /// <summary>Relaunch first (on a row of its own when the row is too narrow), then the code at a fixed width; Copy, Invite
    /// and Leave at the right edge.</summary>
    private void ArrangeLobbyRow()
    {
        var height = Theme.ControlHeight;
        var codeY = 0;
        var codeX = 0;
        if (_view is { Relaunch: true })
        {
            _relaunch.SetBounds(0, 0, Theme.PrimaryWidth, height);
            if (RelaunchWraps) codeY = height + Theme.Gap;
            else codeX = Theme.PrimaryWidth + Theme.Gap;
        }
        _code.SetBounds(codeX, codeY, CodeWidth, height);
        var right = _lobbyRow.Width;
        foreach (var button in new[] { _leave, _invite, _copy })
        {
            right -= Theme.ToolWidth;
            button.SetBounds(right, codeY, Theme.ToolWidth, height);
            right -= Theme.Gap;
        }
    }
}
