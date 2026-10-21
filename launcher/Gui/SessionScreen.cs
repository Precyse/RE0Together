namespace CoopLauncher.Gui;

/// <summary>The hosting screen and the lobby (one control, two looks). A left column with the logo, the lobby code and its copy
/// button, then the step list and START GAME while hosting, or the state line, the host's options and the last events with MINI
/// MODE (RELAUNCH when the game closed) in the lobby; Leave under either and a notice row under the buttons. A right column
/// holds the players as cards (a PLAYERS n/max caption while hosting). The game's art lies under it all; everything is centred in
/// the window both ways. Shown with a <see cref="SessionModel"/>; reports every button, and the character of the local card.</summary>
internal sealed class SessionScreen : Control
{
    private const float HostingBackdrop = 0.18f;
    private const float LobbyBackdrop = 0.20f;
    private const string CodeCaption = "LOBBY CODE";
    private const int EventLines = 2;

    private static readonly int ContentWidth = Theme.Scale(760);
    private static readonly int HostingColumn = Theme.Scale(330);
    private static readonly int HostingGap = Theme.Scale(48);
    private static readonly int LobbyColumn = Theme.Scale(300);
    private static readonly int LobbyGap = Theme.Scale(40);
    private static readonly int ColumnGap = Theme.Scale(20);
    private static readonly Size LogoBox = new(Theme.Scale(200), Theme.Scale(80));
    private static readonly int CaptionGap = Theme.Scale(6);
    private static readonly int CodeRow = Theme.Scale(40);
    private static readonly int CopyGap = Theme.Scale(8);
    private static readonly int HostingRowGap = Theme.Scale(12);
    private static readonly int StateIcon = Theme.Scale(18);
    private static readonly int StateGap = Theme.Scale(12);
    private static readonly int TimerGap = Theme.Scale(14);
    private static readonly int OptionGap = Theme.Scale(14);
    private static readonly int LineGap = Theme.Scale(6);
    private static readonly int PlayersGap = Theme.Scale(12);
    private static readonly Size PrimarySize = new(Theme.Scale(150), Theme.Scale(44));
    private static readonly Size LeaveSize = new(Theme.Scale(104), Theme.Scale(44));
    private static readonly int ButtonGap = Theme.Scale(12);
    private static readonly int CheckMs = Durations.CheckDrawDelay + Durations.CheckDraw;

    private readonly BackdropLayer _backdrop = new();
    private readonly StepList _steps = new(StepSize.Large) { RowGap = HostingRowGap, Background = Theme.Bg };
    private readonly PlayerCardGrid _grid;
    private readonly CopyCodeButton _copy = new();
    private readonly FlatButton _primary = new(string.Empty, ButtonKind.Primary) { Size = PrimarySize };
    private readonly FlatButton _leave = new("Leave", ButtonKind.Ghost) { Size = LeaveSize };
    private readonly NoticeRow _notice = new();
    private readonly ReelText _counter;
    private readonly StatusLine _state;
    private readonly StatusLine _timer;
    private readonly StatusLine[] _events;
    private SteamArt? _art;
    private SessionModel? _model;
    private Rectangle _logo;
    private Rectangle _caption;
    private Rectangle _code;
    private Rectangle _stateLine;
    private Rectangle _options;
    private Rectangle _eventLines;
    private Rectangle _players;
    private double _checkMs = CheckMs;
    private double _spin;
    private LobbyStateKind? _kind;

    public SessionScreen(AvatarStore avatars, OutfitArtStore art)
    {
        _grid = new PlayerCardGrid(avatars, art);
        _counter = new ReelText(this, "players");
        _state = new StatusLine(this, "state");
        _timer = new StatusLine(this, "timer");
        _events = Enumerable.Range(0, EventLines).Select(i => new StatusLine(this, $"event{i}")).ToArray();
        BackColor = Theme.Bg;
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
        Controls.AddRange([_grid, _steps, _copy, _primary, _leave, _notice]);
        _primary.Click += (_, _) => PrimaryClicked();
        _leave.Click += (_, _) => LeaveClicked?.Invoke();
        _copy.Click += (_, _) => CopyClicked?.Invoke();
        _notice.ActionClicked += () => NoticeActionClicked?.Invoke();
        _notice.ReportClicked += () => NoticeReportClicked?.Invoke();
        _grid.SeatClicked += OnSeatClicked;
    }

    public event Action? StartClicked;
    public event Action? LeaveClicked;
    public event Action? CopyClicked;
    public event Action? MiniClicked;
    public event Action? RelaunchClicked;
    public event Action? NoticeActionClicked;
    public event Action? NoticeReportClicked;

    /// <summary>The local player's card was clicked: its character's label (a game with a character select).</summary>
    public event Action<string>? SeatClicked;

    public void Show(SessionModel model, SteamArt art)
    {
        var first = _model == null;
        var modeChanged = _model != null && _model.Hosting != model.Hosting;
        _model = model;
        _art = art;
        _primary.Text = model.Hosting ? "START GAME" : model.Relaunch ? "RELAUNCH" : "MINI MODE";
        _primary.Enabled = !model.Hosting || model.StartEnabled;
        _steps.Visible = model.Hosting;
        if (model.Hosting) _steps.Show(model.Steps);
        _counter.Set(model.Counter);
        ShowLobbyState(model);
        _grid.Layout = model.Layout;
        _grid.Clickable = model.Seats.Any(card => card.Local && card.Figure != null);
        _grid.Show(model.Seats);
        if (first || modeChanged) _grid.Enter();
        _notice.Show(model.Notice);
        Arrange();
        Invalidate();
    }

    protected override void OnResize(EventArgs e)
    {
        Arrange();
        base.OnResize(e);
    }

    protected override void OnPaintBackground(PaintEventArgs e) =>
        _backdrop.Paint(e.Graphics, ClientSize, _art?.Hero, _model is { Hosting: true } ? HostingBackdrop : LobbyBackdrop);

    protected override void OnPaint(PaintEventArgs e)
    {
        if (_model == null) return;
        var g = e.Graphics;
        SessionArt.PaintLogo(g, _art?.Logo, _logo);
        Draw.TextSpaced(g, CodeCaption, Theme.Label, Theme.Muted, _caption, Theme.Scale(1));
        Draw.Text(g, _model.Code, _model.Hosting ? Theme.CodeLarge : Theme.CodeMedium, Theme.Text, _code);
        if (_model.Hosting)
        {
            _counter.Paint(g, _players with { Height = Theme.Label.Height }, Theme.Label, Theme.Muted, Theme.Bg);
            return;
        }
        PaintState(g);
        PaintOptions(g);
        for (var i = 0; i < _events.Length; i++)
            _events[i].Paint(g, EventRect(i), Theme.Mono, Theme.Muted, Theme.Bg);
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing) _backdrop.Dispose();
        base.Dispose(disposing);
    }

    private void PrimaryClicked()
    {
        if (_model is not { } model) return;
        if (model.Hosting) StartClicked?.Invoke();
        else if (model.Relaunch) RelaunchClicked?.Invoke();
        else MiniClicked?.Invoke();
    }

    private void OnSeatClicked(int index)
    {
        if (_model is not { } model || index >= model.Seats.Count || !model.Seats[index].Local || model.Seats[index].Figure == null) return;
        SeatClicked?.Invoke(SessionScreens.CharacterOf(model.Seats[index]));
    }

    // ---------------------------------------------------------------- the lobby's state line

    private void ShowLobbyState(SessionModel model)
    {
        var state = model.State;
        _state.Set(state?.Words ?? string.Empty);
        _timer.Set(state?.Timer ?? string.Empty);
        for (var i = 0; i < _events.Length; i++) _events[i].Set(i < model.Events.Count ? model.Events[i] : string.Empty);
        var kind = state?.Kind;
        if (kind == _kind) return;
        var arrived = kind == LobbyStateKind.InGame && _kind != null;
        _kind = kind;
        SpinWhile(kind is LobbyStateKind.Reconnecting or LobbyStateKind.WaitingForHost);
        _checkMs = CheckMs;
        if (arrived) DrawCheck();
    }

    private void DrawCheck()
    {
        _checkMs = 0;
        Motion.Animate(this, "check", 0, CheckMs, CheckMs, Easing.Linear, ms =>
        {
            _checkMs = ms;
            Invalidate(_stateLine);
        });
    }

    private void SpinWhile(bool spinning)
    {
        if (!spinning)
        {
            Motion.Cancel(this, "spin");
            return;
        }
        Motion.Loop(this, "spin", Durations.Loop, phase =>
        {
            _spin = phase;
            Invalidate(_stateLine);
        });
    }

    private void PaintState(Graphics g)
    {
        var line = _stateLine;
        var icon = new Rectangle(line.X, line.Y + (line.Height - StateIcon) / 2, StateIcon, StateIcon);
        switch (_model!.State?.Kind)
        {
            case LobbyStateKind.InGame:
                PaintCheck(g, icon);
                break;
            case LobbyStateKind.GameClosed:
                Draw.Cross(g, Theme.Problem, icon);
                break;
            default:
                Spinner.Paint(g, icon, _spin, SpinnerSize.Large);
                break;
        }
        var textX = icon.Right + StateGap;
        var words = Math.Max(Draw.Width(_state.Text, Theme.Title), Draw.Width(_model.State?.Words ?? string.Empty, Theme.Title));
        _state.Paint(g, new Rectangle(textX, line.Y, words, line.Height), Theme.Title, Theme.Text, Theme.Bg);
        var timerX = textX + words + TimerGap;
        _timer.Paint(g, new Rectangle(timerX, line.Y, Math.Max(0, line.Right - timerX), line.Height), Theme.MonoMedium, Theme.Muted, Theme.Bg);
    }

    /// <summary>The check drawn in as a step's is (success-check).</summary>
    private void PaintCheck(Graphics g, Rectangle icon)
    {
        var (opacity, rotation, bob, drawn) = StepPainter.CheckFrame(_checkMs);
        var state = g.Save();
        g.TranslateTransform(icon.X + icon.Width / 2f, icon.Y + icon.Height / 2f + Theme.ScaleF((float)bob));
        g.RotateTransform((float)rotation);
        Draw.Check(g, Color.FromArgb((int)Math.Round(255 * opacity), Theme.Connected), new Rectangle(-icon.Width / 2, -icon.Height / 2, icon.Width, icon.Height), drawn);
        g.Restore(state);
    }

    private void PaintOptions(Graphics g)
    {
        var pairs = _model!.Options;
        if (pairs.Count == 0) return;
        var labels = pairs.Max(pair => Draw.Width(pair.Label, Theme.StepSmall));
        for (var i = 0; i < pairs.Count; i++)
        {
            var y = _options.Y + i * (Theme.StepSmall.Height + LineGap);
            Draw.Text(g, pairs[i].Label, Theme.StepSmall, Theme.Muted, new Rectangle(_options.X, y, labels, Theme.StepSmall.Height));
            Draw.Text(g, pairs[i].Value, Theme.StepSmall, Theme.Text, new Rectangle(_options.X + labels + OptionGap, y, Math.Max(0, _options.Width - labels - OptionGap), Theme.StepSmall.Height));
        }
    }

    private Rectangle EventRect(int index) =>
        new(_eventLines.X, _eventLines.Y + index * (Theme.Mono.Height + LineGap), _eventLines.Width, Theme.Mono.Height);

    // ---------------------------------------------------------------- layout

    /// <summary>Stacks both columns and centres each in the window vertically.</summary>
    private void Arrange()
    {
        if (_model is not { } model) return;
        var hosting = model.Hosting;
        var content = Math.Min(ContentWidth, Width);
        var left = (Width - content) / 2;
        var leftWidth = hosting ? HostingColumn : LobbyColumn;
        var right = left + leftWidth + (hosting ? HostingGap : LobbyGap);
        ArrangeLeft(model, left, leftWidth);
        ArrangeRight(model, right, Math.Max(0, left + content - right));
    }

    private void ArrangeLeft(SessionModel model, int x, int width)
    {
        var captionHeight = Theme.Label.Height;
        var stateHeight = Theme.Title.Height;
        var optionRows = model.Options.Count;
        var optionsHeight = optionRows == 0 ? 0 : optionRows * Theme.StepSmall.Height + (optionRows - 1) * LineGap;
        var eventCount = model.Events.Count;
        var eventsHeight = eventCount == 0 ? 0 : eventCount * Theme.Mono.Height + (eventCount - 1) * LineGap;
        var infoHeight = optionsHeight + (optionsHeight > 0 && eventsHeight > 0 ? OptionGap : 0) + eventsHeight;
        var middle = model.Hosting ? _steps.Height : stateHeight + (infoHeight > 0 ? ColumnGap + infoHeight : 0);
        var total = LogoBox.Height + ColumnGap + captionHeight + CaptionGap + CodeRow + ColumnGap + middle + ColumnGap + PrimarySize.Height
            + (model.Notice != null ? ColumnGap + Theme.ControlHeight : 0);

        var y = Math.Max(0, (Height - total) / 2);
        _logo = new Rectangle(x, y, LogoBox.Width, LogoBox.Height);
        y += LogoBox.Height + ColumnGap;
        _caption = new Rectangle(x, y, width, captionHeight);
        y += captionHeight + CaptionGap;
        var font = model.Hosting ? Theme.CodeLarge : Theme.CodeMedium;
        var codeWidth = Draw.TightWidth(model.Code, font);
        _code = new Rectangle(x, y, Math.Min(width, Draw.Width(model.Code, font)), CodeRow);
        _copy.Location = new Point(x + Math.Min(codeWidth + CopyGap, width - _copy.Width), y);
        y += CodeRow + ColumnGap;
        if (model.Hosting)
        {
            _steps.SetBounds(x, y, width, _steps.Height);
            y += _steps.Height + ColumnGap;
        }
        else
        {
            _stateLine = new Rectangle(x, y, width, stateHeight);
            y += stateHeight + ColumnGap;
            _options = new Rectangle(x, y, width, optionsHeight);
            _eventLines = new Rectangle(x, y + optionsHeight + (optionsHeight > 0 ? OptionGap : 0), width, eventsHeight);
            y += infoHeight > 0 ? infoHeight + ColumnGap : 0;
        }
        _primary.Location = new Point(x, y);
        _leave.Location = new Point(x + PrimarySize.Width + ButtonGap, y);
        y += PrimarySize.Height;
        _notice.SetBounds(x, y + ColumnGap, width, Theme.ControlHeight);
    }

    private void ArrangeRight(SessionModel model, int x, int width)
    {
        var caption = model.Hosting ? Theme.Label.Height + PlayersGap : 0;
        var y = Math.Max(0, (Height - caption - _grid.Height) / 2);
        _players = new Rectangle(x, y, width, Theme.Label.Height);
        _grid.Location = new Point(x, y + caption);
        _grid.Width = width;
    }
}
