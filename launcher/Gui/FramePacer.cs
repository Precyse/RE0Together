using System.Runtime.InteropServices;

namespace CoopLauncher.Gui;

/// <summary>Calls a tick on the UI thread once per display refresh while running. A background thread waits for the
/// compositor's next frame (DwmFlush, which follows the monitor's refresh rate: 60, 144 Hz...) and posts the tick to the
/// UI thread, at most one at a time so a slow frame never queues a backlog. Idle, the thread sleeps on an event and uses
/// no CPU. Without a UI synchronization context (unit tests) it only tracks the running flag.</summary>
internal sealed class FramePacer
{
    private const int FallbackFrameMs = 4;
    private const string ThreadName = "motion-frames";

    private readonly Action _tick;
    private readonly SynchronizationContext? _ui;
    private readonly AutoResetEvent _wake = new(false);
    private Thread? _thread;
    private volatile bool _running;
    private int _posted;

    public FramePacer(SynchronizationContext? ui, Action tick)
    {
        _ui = ui;
        _tick = tick;
    }

    public bool Running => _running;

    public void Start()
    {
        if (_running) return;
        _running = true;
        if (_ui == null) return;
        if (_thread == null)
        {
            _thread = new Thread(Loop) { IsBackground = true, Name = ThreadName, Priority = ThreadPriority.AboveNormal };
            _thread.Start();
        }
        _wake.Set();
    }

    public void Stop() => _running = false;

    private void Loop()
    {
        while (true)
        {
            if (!_running)
            {
                _wake.WaitOne();
                continue;
            }
            WaitForNextFrame();
            if (!_running || Interlocked.Exchange(ref _posted, 1) == 1) continue;
            _ui!.Post(_ =>
            {
                Interlocked.Exchange(ref _posted, 0);
                if (_running) _tick();
            }, null);
        }
    }

    /// <summary>Blocks until the compositor presents its next frame; if composition is unavailable, sleeps briefly.</summary>
    private static void WaitForNextFrame()
    {
        if (DwmFlush() != 0) Thread.Sleep(FallbackFrameMs);
    }

    [DllImport("dwmapi.dll")]
    private static extern int DwmFlush();
}
