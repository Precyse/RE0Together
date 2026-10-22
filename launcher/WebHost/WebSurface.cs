using CoopLauncher.Gui;
using Microsoft.Web.WebView2.Core;
using Microsoft.Web.WebView2.WinForms;

namespace CoopLauncher.Web;

/// <summary>One hardened WebView2 showing one of the pages (SPEC 1 and 4). The environment is shared by every surface. Inbound:
/// only messages whose source is the app page and that <see cref="UiCommands"/> accepts. Outbound: data only, never script;
/// snapshots are built when they are posted (at most one pending post, an identical one is skipped), nothing is posted while the
/// surface is suspended, and the latest snapshot goes out when it resumes. Navigation, new windows, downloads and permissions are
/// refused. A render process that fails reloads the page; a lost browser process recreates the view once, a second loss is
/// reported through <see cref="Failed"/>.</summary>
internal sealed class WebSurface : IDisposable
{
    /// <summary>The most messages (log lines, events) kept while the surface cannot post; the page keeps no more than 500 lines.</summary>
    public const int MaxBuffered = 500;

    private static readonly object EnvironmentGate = new();
    private static Task<CoreWebView2Environment>? _environment;
    private static int _environmentGeneration;

    private readonly PageKind _page;
    private readonly WebAssets _assets;
    private readonly Func<string> _buildState;
    private readonly Queue<string> _outbox = new();
    private readonly Control _host = new() { Dock = DockStyle.Fill, BackColor = Theme.Bg };
    private WebView2? _view;
    private int _generation;
    private bool _recreated;
    private bool _ready;
    private bool _dirty;
    private bool _flushQueued;
    private bool _suspended;
    private bool _disposed;
    private string _lastState = string.Empty;
    private long _seq;

    public WebSurface(PageKind page, WebAssets assets, Func<string> buildState)
    {
        _page = page;
        _assets = assets;
        _buildState = buildState;
    }

    /// <summary>The control the shell places in its window; it holds the web view.</summary>
    public Control Host => _host;

    /// <summary>The page reported <c>ready</c>: send <c>hello</c>, the log and the first state.</summary>
    public event Action? PageReady;

    /// <summary>A command from the page that passed the origin check and the schema.</summary>
    public event Action<UiCommand>? CommandReceived;

    /// <summary>The web view cannot be kept running (the browser process was lost twice).</summary>
    public event Action? Failed;

    /// <summary>Whether the page reported ready since it was last loaded.</summary>
    public bool IsReady => _ready;

    public bool IsSuspended => _suspended;

    public DropLog Drops { get; } = DropLog.ToLogFile();

    public CoreWebView2? Core => _view?.CoreWebView2;

    /// <summary>Starts the web view and loads the page; false when the runtime could not be started.</summary>
    public async Task<bool> StartAsync()
    {
        try
        {
            await CreateViewAsync();
            return true;
        }
        catch (Exception e) when (e is WebView2RuntimeNotFoundException or InvalidOperationException or System.Runtime.InteropServices.COMException or ArgumentException)
        {
            Log.Info($"The web view could not start: {e.Message}");
            return false;
        }
    }

    /// <summary>The page needs a new snapshot (built when it is posted).</summary>
    public void RequestState()
    {
        _dirty = true;
        ScheduleFlush();
    }

    /// <summary>Queues a message (hello, log lines, an event) ahead of the next snapshot; kept while suspended.</summary>
    public void Post(string json)
    {
        _outbox.Enqueue(json);
        while (_outbox.Count > MaxBuffered) _outbox.Dequeue();
        ScheduleFlush();
    }

    /// <summary>The window is hidden or minimized: the view is hidden, throttled and suspended, and nothing is posted.</summary>
    public async void Suspend()
    {
        if (_suspended || _view?.CoreWebView2 is not { } core) return;
        _suspended = true;
        _view.Visible = false;
        core.MemoryUsageTargetLevel = CoreWebView2MemoryUsageTargetLevel.Low;
        try
        {
            await core.TrySuspendAsync();
        }
        catch (Exception e) when (e is InvalidOperationException or System.Runtime.InteropServices.COMException or ObjectDisposedException)
        {
            Log.FileOnly($"The web view could not be suspended: {e.Message}");
        }
    }

    /// <summary>The window shows again: the view wakes and the latest snapshot and the buffered messages go out.</summary>
    public void Resume()
    {
        if (!_suspended) return;
        _suspended = false;
        if (_view?.CoreWebView2 is { } core)
        {
            _view.Visible = true;
            core.Resume();
            core.MemoryUsageTargetLevel = CoreWebView2MemoryUsageTargetLevel.Normal;
        }
        _dirty = true;
        ScheduleFlush();
    }

    public void Dispose()
    {
        _disposed = true;
        _view?.Dispose();
        _host.Dispose();
    }

    // ---------------------------------------------------------------- the view

    private async Task CreateViewAsync()
    {
        var environment = await SharedEnvironment();
        var view = new WebView2 { Dock = DockStyle.Fill, DefaultBackgroundColor = Theme.Bg, AllowExternalDrop = false };
        _view = view;
        _host.Controls.Add(view);
        _ = view.Handle;   // creates the handles up the chain: the view starts even in a window that is not shown yet
        _generation = _environmentGeneration;
        await view.EnsureCoreWebView2Async(environment);
        Harden(view.CoreWebView2);
        _assets.Attach(view.CoreWebView2);
        view.CoreWebView2.WebMessageReceived += OnMessage;
        view.CoreWebView2.ProcessFailed += OnProcessFailed;
        view.CoreWebView2.Navigate(WebHosts.AppOrigin + (_page == PageKind.Main ? "index.html" : "mini.html"));
    }

    private static Task<CoreWebView2Environment> SharedEnvironment()
    {
        lock (EnvironmentGate)
            return _environment ??= CoreWebView2Environment.CreateAsync(null, AppData.WebViewDir, new CoreWebView2EnvironmentOptions());
    }

    private static void Harden(CoreWebView2 core)
    {
        var settings = core.Settings;
        settings.AreHostObjectsAllowed = false;
        settings.IsWebMessageEnabled = true;
        settings.IsStatusBarEnabled = false;
        settings.IsZoomControlEnabled = false;
        settings.IsPinchZoomEnabled = false;
        settings.IsSwipeNavigationEnabled = false;
        settings.AreDefaultScriptDialogsEnabled = false;
        settings.IsGeneralAutofillEnabled = false;
        settings.IsPasswordAutosaveEnabled = false;
        settings.IsReputationCheckingRequired = false;
        settings.IsNonClientRegionSupportEnabled = true;
#if DEBUG
        settings.AreDevToolsEnabled = true;
        settings.AreDefaultContextMenusEnabled = true;
        settings.AreBrowserAcceleratorKeysEnabled = true;
#else
        settings.AreDevToolsEnabled = false;
        settings.AreDefaultContextMenusEnabled = false;
        settings.AreBrowserAcceleratorKeysEnabled = false;
#endif
        core.NavigationStarting += (_, e) => e.Cancel = !e.Uri.StartsWith(WebHosts.AppOrigin, StringComparison.Ordinal);
        core.FrameNavigationStarting += (_, e) => e.Cancel = true;
        core.NewWindowRequested += (_, e) => e.Handled = true;
        core.DownloadStarting += (_, e) =>
        {
            e.Cancel = true;
            e.Handled = true;
        };
        core.PermissionRequested += (_, e) => e.State = CoreWebView2PermissionState.Deny;
    }

    // ---------------------------------------------------------------- inbound

    private void OnMessage(object? sender, CoreWebView2WebMessageReceivedEventArgs e)
    {
        if (!UiCommands.OriginAllowed(e.Source))
        {
            Drops.Report("origin", "message from outside the app page");
            return;
        }
        if (!UiCommands.Parse(e.WebMessageAsJson, out var command, out var error, out var type))
        {
            Drops.Report(type, error);
            return;
        }
        if (command!.Type == Cmd.Ready)
        {
            _ready = true;
            _lastState = string.Empty;
            PageReady?.Invoke();
            return;
        }
        CommandReceived?.Invoke(command);
    }

    private async void OnProcessFailed(object? sender, CoreWebView2ProcessFailedEventArgs e)
    {
        if (_disposed) return;
        if (e.ProcessFailedKind != CoreWebView2ProcessFailedKind.BrowserProcessExited)
        {
            Log.Info($"The web view's {e.ProcessFailedKind} failed; reloading the page");
            _ready = false;
            _view?.CoreWebView2?.Reload();
            return;
        }
        if (_recreated)
        {
            Failed?.Invoke();
            return;
        }
        _recreated = true;
        Log.Info("The web view's browser process exited; starting it again");
        await RecreateAsync();
    }

    private async Task RecreateAsync()
    {
        lock (EnvironmentGate)
        {
            if (_generation == _environmentGeneration)
            {
                _environmentGeneration++;
                _environment = null;
            }
        }
        _ready = false;
        _suspended = false;
        var old = _view;
        _view = null;
        if (old != null)
        {
            _host.Controls.Remove(old);
            old.Dispose();
        }
        try
        {
            await CreateViewAsync();
        }
        catch (Exception e) when (e is WebView2RuntimeNotFoundException or InvalidOperationException or System.Runtime.InteropServices.COMException)
        {
            Log.Info($"The web view could not start again: {e.Message}");
            Failed?.Invoke();
        }
    }

    // ---------------------------------------------------------------- outbound

    private void ScheduleFlush()
    {
        if (_flushQueued || _disposed || !_host.IsHandleCreated) return;
        _flushQueued = true;
        _host.BeginInvoke(Flush);
    }

    private void Flush()
    {
        _flushQueued = false;
        if (_suspended || !_ready || _disposed || _view?.CoreWebView2 is not { } core) return;
        while (_outbox.Count > 0) core.PostWebMessageAsJson(_outbox.Dequeue());
        if (!_dirty) return;
        _dirty = false;
        var state = _buildState();
        if (state == _lastState) return;
        _lastState = state;
        core.PostWebMessageAsJson(UiMessages.State(++_seq, state));
    }
}
