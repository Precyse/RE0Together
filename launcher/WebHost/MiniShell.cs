using CoopLauncher.Gui;

namespace CoopLauncher.Web;

/// <summary>The mini mode window (SPEC 1): a small always-on-top window, with its own taskbar button, at the bottom right of the main
/// window's screen, showing the mini page. It is made on the first MINI MODE and kept hidden for reuse, and shown only once its
/// page has drawn the first mini state, so that no empty frame shows; the page then plays the open. Expand sends <c>miniClose</c>
/// and the window hides when the page answers <c>mini.closed</c> or after <see cref="CloseTimeoutMs"/>. The page's header drags the
/// window (<c>app-region</c>); Alt+F4 and closing it from the taskbar act as Expand.</summary>
internal sealed class MiniShell : Form
{
    public const int CloseTimeoutMs = 400;

    private const int FirstRenderTimeoutMs = 2000;
    private const int ReopenRenderTimeoutMs = 150;
    private const int ClientWidth = 360;
    private const int ClientHeight = 220;
    private const string WindowTitle = "Co-op";
    private const int ScreenMargin = 16;

    private readonly UiController _controller;
    private readonly WebSurface _surface;
    private readonly System.Windows.Forms.Timer _timer = new();
    private Form? _main;
    private Action? _onShown;
    private Action? _onClosed;
    private bool _opening;
    private bool _closing;
    private bool _placed;
    private bool _started;
    private bool _renderedOnce;

    public MiniShell(UiController controller, WebAssets assets)
    {
        _controller = controller;
        FormBorderStyle = FormBorderStyle.None;
        StartPosition = FormStartPosition.Manual;
        TopMost = true;
        ShowInTaskbar = true;
        Text = WindowTitle;
        BackColor = Theme.Bg;
        ClientSize = new Size(Theme.Scale(ClientWidth), Theme.Scale(ClientHeight));
        _surface = new WebSurface(PageKind.Mini, assets, () => UiJson.Write(_controller.BuildMini()));
        Controls.Add(_surface.Host);
        _surface.PageReady += OnPageReady;
        _surface.CommandReceived += OnCommand;
        _surface.Failed += () => _controller.ExpandMini();
        _timer.Tick += (_, _) => OnTimer();
    }

    /// <summary>Opens the window next to the main window; it shows once its page has drawn the first state. <paramref name="onShown"/>
    /// runs when it is on screen (the main window hides then).</summary>
    public void Open(Form main, Action onShown)
    {
        _main = main;
        _onShown = onShown;
        _onClosed = null;
        _opening = true;
        _closing = false;
        if (!_started)
        {
            _started = true;
            _ = StartAsync();
        }
        _surface.Resume();
        _surface.RequestState();
        StartTimer(_renderedOnce ? ReopenRenderTimeoutMs : FirstRenderTimeoutMs);
    }

    /// <summary>Closes the window: the page plays its close, then the window hides and <paramref name="onClosed"/> runs.</summary>
    public void Close(Action onClosed)
    {
        _opening = false;
        if (!Visible)
        {
            onClosed();
            return;
        }
        _closing = true;
        _onClosed = onClosed;
        _surface.Post(UiMessages.Event(UiMessages.MiniClose));
        StartTimer(CloseTimeoutMs);
    }

    public void RequestState() => _surface.RequestState();

    protected override void OnFormClosing(FormClosingEventArgs e)
    {
        if (e.CloseReason == CloseReason.UserClosing)
        {
            e.Cancel = true;
            _controller.ExpandMini();
        }
        base.OnFormClosing(e);
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            _timer.Dispose();
            _surface.Dispose();
        }
        base.Dispose(disposing);
    }

    private async Task StartAsync()
    {
        _ = Handle;
        if (!await _surface.StartAsync()) _controller.ExpandMini();
    }

    private void OnPageReady()
    {
        _surface.Post(UiMessages.Hello(PageKind.Mini, debug: false, ReducedMotion.Reduced));
        _surface.RequestState();
    }

    private void OnCommand(UiCommand command)
    {
        switch (command.Type)
        {
            case Cmd.PageRendered:
                _renderedOnce = true;
                if (_opening && !Visible) ShowNow();
                return;
            case Cmd.MiniClosed:
                if (_closing) HideNow();
                return;
            default:
                BeginInvoke(() => _controller.Run(command, _surface.Drops));
                return;
        }
    }

    private void StartTimer(int milliseconds)
    {
        _timer.Stop();
        _timer.Interval = milliseconds;
        _timer.Start();
    }

    /// <summary>The page did not answer in time: a window waiting to open shows anyway, one waiting to close hides.</summary>
    private void OnTimer()
    {
        _timer.Stop();
        if (_closing) HideNow();
        else if (_opening && !Visible) ShowNow();
    }

    /// <summary>The corner of a work area where the window sits: bottom right, 16 px from both edges.</summary>
    private static Point Corner(Rectangle workArea, Size size) =>
        new(workArea.Right - size.Width - Theme.Scale(ScreenMargin), workArea.Bottom - size.Height - Theme.Scale(ScreenMargin));

    private void ShowNow()
    {
        _timer.Stop();
        _opening = false;
        if (!_placed && _main != null)
        {
            Location = Corner(Screen.FromControl(_main).WorkingArea, Size);
            _placed = true;
        }
        Show();
        _surface.Resume();
        _surface.Post(UiMessages.Event(UiMessages.Shown));
        _onShown?.Invoke();
    }

    private void HideNow()
    {
        _timer.Stop();
        _closing = false;
        Hide();
        _surface.Suspend();
        var closed = _onClosed;
        _onClosed = null;
        closed?.Invoke();
    }
}
