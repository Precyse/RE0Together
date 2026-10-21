using System.Diagnostics;
using System.Runtime.InteropServices;

namespace CoopLauncher;

/// <summary>
/// One launcher window per user session. A second start asks the running window to come to the front and exits. The window
/// answers from the moment it owns the mutex (the listener is registered in <see cref="Acquire"/>, before anything slow runs),
/// so a start that is still busy is never mistaken for a hung one. Only a copy that stays silent for <see cref="HungAfterMs"/>
/// and is hung (InstanceGuard) is closed and replaced; otherwise the second start just exits.
/// An update relaunch names its predecessor in the environment and waits for it to exit first.
/// </summary>
public sealed class SingleInstance : IDisposable
{
    public const string PredecessorVariable = "COOP_LAUNCHER_PREDECESSOR";
    private const string MutexName = @"Local\CoopLauncher.Window";
    private const string FocusRequestName = @"Local\CoopLauncher.FocusRequest";
    private const string FocusAnswerName = @"Local\CoopLauncher.FocusAnswer";
    private const int AnswerWaitMs = 3000;
    public const int HungAfterMs = 10_000;
    private const int TakeOverWaitMs = 5000;
    private const int PredecessorWaitMs = 10000;
    private const uint AnyProcess = uint.MaxValue;

    private readonly Mutex _mutex;
    private readonly bool _owned;
    private readonly EventWaitHandle _focusRequest = new(false, EventResetMode.AutoReset, FocusRequestName);
    private readonly EventWaitHandle _focusAnswer = new(false, EventResetMode.AutoReset, FocusAnswerName);
    private readonly object _showGate = new();
    private readonly RegisteredWaitHandle _listener;
    private Action? _show;
    private bool _showQueued;

    private SingleInstance(Mutex mutex, bool owned)
    {
        _mutex = mutex;
        _owned = owned;
        _listener = ThreadPool.RegisterWaitForSingleObject(_focusRequest, (_, _) => OnFocusRequest(), null, Timeout.Infinite, executeOnlyOnce: false);
    }

    /// <summary>This process as the running launcher window, or null when this start must exit: another window took the request
    /// to come forward, or the mutex holder is silent but not provably hung.</summary>
    public static SingleInstance? Acquire()
    {
        WaitForPredecessor();
        var mutex = new Mutex(initiallyOwned: true, MutexName, out var owned);
        if (owned)
        {
            var window = new SingleInstance(mutex, owned: true);
            InstanceGuard.CloseOtherLaunchers(holdsWindow: false, answered: false);
            return window;
        }
        if (AskRunningWindowToShow(AnswerWaitMs) || AskRunningWindowToShow(HungAfterMs))
        {
            mutex.Dispose();
            return null;
        }
        Log.Info($"The running launcher has not answered for {AnswerWaitMs + HungAfterMs} ms");
        if (InstanceGuard.CloseOtherLaunchers(holdsWindow: true, answered: false) > 0 && TakeOver(mutex)) return new SingleInstance(mutex, owned: true);
        mutex.Dispose();
        return null;
    }

    /// <summary>Calls the action (on a pool thread) whenever a second start asks this window to come forward, once at once if one
    /// already asked before the window existed.</summary>
    public void OnShowRequested(Action show)
    {
        bool runNow;
        lock (_showGate)
        {
            _show = show;
            runNow = _showQueued;
            _showQueued = false;
        }
        if (runNow) show();
    }

    public void Dispose()
    {
        _listener.Unregister(null);
        _focusRequest.Dispose();
        _focusAnswer.Dispose();
        if (_owned) _mutex.ReleaseMutex();
        _mutex.Dispose();
    }

    [DllImport("user32.dll")]
    private static extern bool AllowSetForegroundWindow(uint processId);

    /// <summary>Answers a second start at once, then brings the window forward (queued until the window exists).</summary>
    private void OnFocusRequest()
    {
        Action? show;
        lock (_showGate)
        {
            show = _show;
            if (show == null) _showQueued = true;
        }
        _focusAnswer.Set();
        show?.Invoke();
    }

    private static bool AskRunningWindowToShow(int waitMs)
    {
        using var request = new EventWaitHandle(false, EventResetMode.AutoReset, FocusRequestName);
        using var answer = new EventWaitHandle(false, EventResetMode.AutoReset, FocusAnswerName);
        AllowSetForegroundWindow(AnyProcess);
        answer.Reset();
        request.Set();
        return answer.WaitOne(waitMs);
    }

    /// <summary>Takes the mutex of a window that was just closed; a mutex abandoned by the killed process is ours too.</summary>
    private static bool TakeOver(Mutex mutex)
    {
        try
        {
            return mutex.WaitOne(TakeOverWaitMs);
        }
        catch (AbandonedMutexException)
        {
            return true;
        }
    }

    private static void WaitForPredecessor()
    {
        var value = Environment.GetEnvironmentVariable(PredecessorVariable);
        Environment.SetEnvironmentVariable(PredecessorVariable, null);
        if (!int.TryParse(value, out var processId)) return;
        try
        {
            using var predecessor = Process.GetProcessById(processId);
            predecessor.WaitForExit(PredecessorWaitMs);
        }
        catch (ArgumentException)
        {
        }
    }
}
