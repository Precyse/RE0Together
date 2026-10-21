using CoopLauncher.Gui;

namespace CoopLauncher.Web;

/// <summary>The launcher window on the web page (SPEC 0 and 1): the existing chromeless window, with one WebView2 filling it under
/// a top band that stays the frame's resize edge (the page's top bar drags the window with <c>app-region</c>). It owns what is
/// native about the window: its size and place, the dark frame, minimize and close (a close in a session is confirmed through
/// the page), the invite raise and flash, the one-second clock, and the hand-over to the mini window. Everything the page shows
/// comes from <see cref="UiController"/>.</summary>
public sealed class WebShell : ChromelessForm
{
    private const string WindowTitle = "Co-op Launcher";
    private const int ClockIntervalMs = 1000;

    private static readonly int WindowWidth = Theme.Scale(840);
    private static readonly int WindowHeight = Theme.Scale(600);
    private static readonly int MinimumHeight = Theme.Scale(540);

    private readonly App _app;
    private readonly UiController _controller;
    private readonly WebAssets _assets;
    private readonly WebSurface _surface;
    private readonly System.Windows.Forms.Timer _clock = new() { Interval = ClockIntervalMs };
    private readonly List<string> _logLines = new();
    private MiniShell? _mini;
    private bool _miniActive;
    private bool _leaveConfirmed;

    public WebShell(App app)
    {
        _app = app;
        Text = WindowTitle;
        Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath);
        AutoScaleMode = AutoScaleMode.None;
        BackColor = Theme.Bg2;
        MinimumSize = SizeFromClientSize(new Size(WindowWidth, MinimumHeight));
        Size = SizeFromClientSize(new Size(WindowWidth, WindowHeight));
        WindowMemory.Restore(this);
        _controller = new UiController(app, OnUiThread);
        _assets = new WebAssets(_controller.Profiles, _controller.Avatars);
        _surface = new WebSurface(PageKind.Main, _assets, () => UiJson.Write(_controller.BuildMain()));
        Controls.Add(_surface.Host);
        UpdateBand();
        WireEvents();
    }

    protected override int CaptionHeight => Theme.ResizeBand;

    /// <summary>A second start of the launcher asks this window to come forward (called from any thread).</summary>
    public void BringForward() => OnUiThread(Raise);

    private void Raise()
    {
        if (WindowState == FormWindowState.Minimized) WindowState = FormWindowState.Normal;
        Activate();
    }

    private void WireEvents()
    {
        _surface.PageReady += OnPageReady;
        _surface.CommandReceived += OnCommand;
        _surface.Failed += OnWebViewLost;
        _controller.Changed += OnControllerChanged;
        _controller.PageEvent += name => _surface.Post(UiMessages.Event(name));
        _controller.InviteAccepted += () =>
        {
            Raise();
            if (ActiveForm != this) WindowFlash.Start(this);
        };
        _controller.RestartRequested += Close;
        _clock.Tick += (_, _) => _controller.Tick();
        Shown += (_, _) => _controller.Shown();
        Activated += (_, _) => _controller.Activated();
        Resize += (_, _) => OnPlacementChanged();
        VisibleChanged += (_, _) => OnPlacementChanged();
    }

    protected override void OnHandleCreated(EventArgs e)
    {
        base.OnHandleCreated(e);
        NativeTheme.DarkFrame(this);
        // Subscribed only once the handle exists: lines logged before it come from the log's history instead of being dropped.
        foreach (var line in Log.Subscribe(OnLogWritten)) AddLogLine(line);
        var events = Log.SubscribeEvents(OnEventLogged);
        _controller.SeedEvents(events);
    }

    protected override async void OnLoad(EventArgs e)
    {
        base.OnLoad(e);
        if (await _surface.StartAsync()) return;
        WebRuntime.Explain();
        Close();
    }

    protected override void OnFormClosing(FormClosingEventArgs e)
    {
        var inSession = _controller.InSession;
        if (inSession && e.CloseReason == CloseReason.UserClosing && !_leaveConfirmed && _surface.IsReady)
        {
            e.Cancel = true;
            _surface.Post(UiMessages.Event(UiMessages.ConfirmClose));
            return;
        }
        if (inSession) Log.Info("Leaving the session, the window was closed");
        WindowMemory.Save(this);
        base.OnFormClosing(e);
    }

    protected override void OnFormClosed(FormClosedEventArgs e)
    {
        base.OnFormClosed(e);
        _clock.Dispose();
        _controller.Dispose();
        Log.Unsubscribe(OnLogWritten);
        Log.UnsubscribeEvents(OnEventLogged);
        _mini?.Dispose();
        _surface.Dispose();
    }

    // ---------------------------------------------------------------- the page

    private void OnPageReady()
    {
        _surface.Post(UiMessages.Hello(PageKind.Main, debug: IsDebugBuild, Motion.Reduced));
        _surface.Post(UiMessages.LogLines(_logLines));
        _surface.RequestState();
    }

    private void OnCommand(UiCommand command)
    {
        switch (command.Type)
        {
            case Cmd.PageRendered:
                return;
            case Cmd.WindowMinimize:
                WindowState = FormWindowState.Minimized;
                return;
            case Cmd.WindowClose:
                AskToClose();
                return;
            case Cmd.CloseConfirm:
                if (!command.Flag("leave")) return;
                _leaveConfirmed = true;
                Close();
                return;
            default:
                BeginInvoke(() => _controller.Run(command, _surface.Drops));
                return;
        }
    }

    /// <summary>The page's close button: in a session the page asks first; otherwise the window closes.</summary>
    private void AskToClose()
    {
        if (_controller.InSession && _surface.IsReady) _surface.Post(UiMessages.Event(UiMessages.ConfirmClose));
        else Close();
    }

    private void OnWebViewLost()
    {
        WebRuntime.Explain();
        _leaveConfirmed = true;
        Close();
    }

    private void OnControllerChanged()
    {
        _clock.Enabled = _controller.NeedsClock;
        SyncMini();
        _surface.RequestState();
        _mini?.RequestState();
    }

    // ---------------------------------------------------------------- the frame and the mini window

    /// <summary>The page sits under the top resize band while the window is not maximized (a maximized one has no band).</summary>
    private void UpdateBand() => Padding = new Padding(0, WindowState == FormWindowState.Maximized ? 0 : Theme.ResizeBand, 0, 0);

    /// <summary>A hidden or minimized window suspends its page; one shown again wakes it.</summary>
    private void OnPlacementChanged()
    {
        UpdateBand();
        if (!Visible || WindowState == FormWindowState.Minimized) _surface.Suspend();
        else _surface.Resume();
    }

    private void SyncMini()
    {
        if (_controller.MiniOpen == _miniActive) return;
        _miniActive = _controller.MiniOpen;
        if (_miniActive)
        {
            _mini ??= new MiniShell(_controller, _assets);
            _mini.Open(this, onShown: Hide);
        }
        else
        {
            _mini?.Close(onClosed: () =>
            {
                Show();
                Raise();
            });
        }
    }

    // ---------------------------------------------------------------- log and events (any thread)

    private void OnLogWritten(string line) => OnUiThread(() =>
    {
        AddLogLine(line);
        if (_surface.IsReady) _surface.Post(UiMessages.LogLines([line]));
    });

    private void OnEventLogged(string message) => OnUiThread(() => _controller.AddEvent(message));

    private void AddLogLine(string line)
    {
        _logLines.Add(line);
        if (_logLines.Count > WebSurface.MaxBuffered) _logLines.RemoveAt(0);
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

    private static bool IsDebugBuild =>
#if DEBUG
        true;
#else
        false;
#endif
}
