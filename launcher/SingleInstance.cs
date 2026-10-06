using System.Diagnostics;
using System.Runtime.InteropServices;

namespace CoopLauncher;

/// <summary>
/// One launcher window per user session. A second start asks the running window to come to the front and exits. A running
/// copy that does not answer (hung, or a build from before this guard) is closed by InstanceGuard and this start takes over.
/// An update relaunch names its predecessor in the environment and waits for it to exit first.
/// </summary>
public sealed class SingleInstance : IDisposable
{
    public const string PredecessorVariable = "COOP_LAUNCHER_PREDECESSOR";
    private const string MutexName = @"Local\CoopLauncher.Window";
    private const string FocusRequestName = @"Local\CoopLauncher.FocusRequest";
    private const string FocusAnswerName = @"Local\CoopLauncher.FocusAnswer";
    private const int AnswerWaitMs = 3000;
    private const int TakeOverWaitMs = 5000;
    private const int PredecessorWaitMs = 10000;
    private const uint AnyProcess = uint.MaxValue;

    private readonly Mutex _mutex;
    private readonly bool _owned;
    private readonly EventWaitHandle _focusRequest = new(false, EventResetMode.AutoReset, FocusRequestName);
    private readonly EventWaitHandle _focusAnswer = new(false, EventResetMode.AutoReset, FocusAnswerName);
    private RegisteredWaitHandle? _listener;

    private SingleInstance(Mutex mutex, bool owned)
    {
        _mutex = mutex;
        _owned = owned;
    }

    /// <summary>This process as the running launcher window, or null when another window took the request to come forward and this start must exit.</summary>
    public static SingleInstance? Acquire()
    {
        WaitForPredecessor();
        var mutex = new Mutex(initiallyOwned: true, MutexName, out var owned);
        if (!owned && AskRunningWindowToShow())
        {
            mutex.Dispose();
            return null;
        }
        InstanceGuard.CloseOtherLaunchers();
        if (!owned) owned = TakeOver(mutex);
        return new SingleInstance(mutex, owned);
    }

    /// <summary>Calls the action (on a pool thread) whenever a second start asks this window to come forward.</summary>
    public void OnShowRequested(Action show) =>
        _listener = ThreadPool.RegisterWaitForSingleObject(_focusRequest, (_, _) =>
        {
            show();
            _focusAnswer.Set();
        }, null, Timeout.Infinite, executeOnlyOnce: false);

    public void Dispose()
    {
        _listener?.Unregister(null);
        _focusRequest.Dispose();
        _focusAnswer.Dispose();
        if (_owned) _mutex.ReleaseMutex();
        _mutex.Dispose();
    }

    [DllImport("user32.dll")]
    private static extern bool AllowSetForegroundWindow(uint processId);

    private static bool AskRunningWindowToShow()
    {
        using var request = new EventWaitHandle(false, EventResetMode.AutoReset, FocusRequestName);
        using var answer = new EventWaitHandle(false, EventResetMode.AutoReset, FocusAnswerName);
        AllowSetForegroundWindow(AnyProcess);
        answer.Reset();
        request.Set();
        return answer.WaitOne(AnswerWaitMs);
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
