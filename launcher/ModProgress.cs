namespace CoopLauncher;

/// <summary>What an install, an uninstall or a launcher update is doing. Done and Failed end a run.</summary>
public enum ModStep { Checking, BackingUp, Copying, Verifying, RemovingOld, Removing, Downloading, Installing, Done, Failed }

/// <summary>One report of a run: the step, the percent of the whole run (0 to 100, 100 only for Done), the file being worked on
/// out of the files of the step (0 of 0 when the step has no file count), and for Failed why.</summary>
public readonly record struct ModProgress(ModStep Step, int Percent, int File, int Files, string? Reason = null);

/// <summary>The percent of a run. Each step owns a band of the whole; inside it the percent follows the bytes done, so a big file moves
/// the bar more than a small one. A step a run does not need is skipped and its band passes at once, so the percent never goes back.</summary>
public static class ProgressMath
{
    public const int Complete = 100;
    private const int BeforeDone = Complete - 1;

    /// <summary>The first and last percent of a step's band.</summary>
    public static (int Start, int End) Band(ModStep step) => step switch
    {
        ModStep.Checking => (0, 10),
        ModStep.BackingUp => (10, 15),
        ModStep.Copying => (15, 60),
        ModStep.Verifying => (60, 90),
        ModStep.RemovingOld => (90, BeforeDone),
        ModStep.Removing => (0, BeforeDone),
        ModStep.Downloading => (0, 90),
        ModStep.Installing => (90, BeforeDone),
        _ => (Complete, Complete),
    };

    /// <summary>The percent <paramref name="done"/> of <paramref name="total"/> bytes into a step.</summary>
    public static int Percent(ModStep step, long done, long total)
    {
        var (start, end) = Band(step);
        if (total <= 0) return start;
        return start + (int)((end - start) * Math.Min(done, total) / total);
    }
}

/// <summary>Turns the work of a run into reports: <see cref="Begin"/> opens a step with its total bytes and files, <see cref="Advance"/>
/// counts a finished file (<see cref="AddBytes"/> only bytes), <see cref="Finish"/> and <see cref="Fail"/> end the run. The percent never goes down, and only Finish
/// reports 100. A step with no files is not reported. Without a sink every call does nothing.</summary>
public sealed class ProgressReporter(IProgress<ModProgress>? sink)
{
    private ModStep _step;
    private bool _active;
    private long _total;
    private long _done;
    private int _files;
    private int _file;
    private int _last;

    public void Begin(ModStep step, long totalBytes, int files)
    {
        _step = step;
        _active = files > 0;
        _total = totalBytes;
        _done = 0;
        _files = files;
        _file = 0;
        if (_active) Send(ProgressMath.Percent(step, 0, totalBytes));
    }

    /// <summary>A file of the step is done; <paramref name="bytes"/> is its weight in the step's total.</summary>
    public void Advance(long bytes)
    {
        if (!_active) return;
        _file++;
        AddBytes(bytes);
    }

    /// <summary>Bytes of the file being worked on are done (a download's chunks).</summary>
    public void AddBytes(long bytes)
    {
        if (!_active) return;
        _done += bytes;
        Send(ProgressMath.Percent(_step, _done, _total));
    }

    public void Finish() => sink?.Report(new ModProgress(ModStep.Done, ProgressMath.Complete, _files, _files));

    public void Fail(string reason) => sink?.Report(new ModProgress(ModStep.Failed, _last, _file, _files, reason));

    private void Send(int percent)
    {
        _last = Math.Max(_last, percent);
        sink?.Report(new ModProgress(_step, _last, _file, _files));
    }
}

/// <summary>Passes reports on at a bounded rate: a change of step, Done and Failed always, others only when the percent moved and
/// <paramref name="intervalMs"/> have passed since the last one sent. Reports come from the worker thread; the inner sink decides
/// which thread sees them.</summary>
public sealed class ThrottledProgress(IProgress<ModProgress> inner, Func<long> nowMs, long intervalMs = ThrottledProgress.DefaultIntervalMs)
    : IProgress<ModProgress>
{
    public const long DefaultIntervalMs = 50;

    private readonly object _gate = new();
    private ModStep? _lastStep;
    private int _lastPercent = -1;
    private long _lastSentMs;

    public void Report(ModProgress value)
    {
        lock (_gate)
        {
            var now = nowMs();
            var ends = value.Step is ModStep.Done or ModStep.Failed;
            var due = value.Step != _lastStep || (value.Percent != _lastPercent && now - _lastSentMs >= intervalMs);
            if (!ends && !due) return;
            _lastStep = value.Step;
            _lastPercent = value.Percent;
            _lastSentMs = now;
            inner.Report(value);
        }
    }
}
