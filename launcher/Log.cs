namespace CoopLauncher;

public static class Log
{
    public const int KeptRuns = 10;

    private const int HistoryLines = 500;
    private const int EventHistoryLines = 50;
    private const int FirstOlderRun = 2;
    private static readonly object Gate = new();
    private static readonly Queue<string> History = new();
    private static readonly Queue<string> EventHistory = new();
    private static StreamWriter? _file;
    private static event Action<string>? Written;
    private static event Action<string>? EventWritten;

    public static void Info(string message) => Write(message, isEvent: false);

    /// <summary>A line meant for the player (someone joined, the lobby opened): an ordinary log line, also sent to the event
    /// listeners as the plain message.</summary>
    public static void Event(string message) => Write(message, isEvent: true);

    /// <summary>A line for the log file only: a stack trace the player does not need on screen.</summary>
    public static void FileOnly(string message)
    {
        lock (Gate) AppendToFile(Stamp(message));
    }

    /// <summary>Also writes every line to the file from now on (the lines so far first). The previous runs' files are kept
    /// (<see cref="RotatePrevious"/>). A file that cannot be written is skipped: logging must never stop the launcher.</summary>
    public static void WriteToFile(string path, string previousPath)
    {
        lock (Gate)
        {
            try
            {
                Directory.CreateDirectory(Path.GetDirectoryName(path)!);
                RotatePrevious(path, previousPath);
                _file = new StreamWriter(new FileStream(path, FileMode.Create, FileAccess.Write, FileShare.ReadWrite)) { AutoFlush = true };
                foreach (var line in History) _file.WriteLine(line);
            }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException)
            {
                _file = null;
                Console.WriteLine($"Log file {path} unavailable: {e.Message}");
            }
        }
    }

    /// <summary>The log of the run before becomes <paramref name="previousPath"/>, the one before that gets "2" appended to the
    /// previous file's name (launcher.prev.log, launcher.prev2.log, ...), and so on up to <see cref="KeptRuns"/> runs; older ones are deleted.</summary>
    public static void RotatePrevious(string path, string previousPath)
    {
        if (!File.Exists(path)) return;
        File.Delete(OlderRun(previousPath, KeptRuns));
        for (var run = KeptRuns - 1; run >= 1; run--)
        {
            var from = OlderRun(previousPath, run);
            if (File.Exists(from)) File.Move(from, OlderRun(previousPath, run + 1), overwrite: true);
        }
        File.Move(path, previousPath, overwrite: true);
    }

    /// <summary>Starts sending new lines to the listener (on the logging thread) and returns the lines written so far,
    /// with none lost or repeated in between.</summary>
    public static IReadOnlyList<string> Subscribe(Action<string> listener)
    {
        lock (Gate)
        {
            Written += listener;
            return History.ToList();
        }
    }

    public static void Unsubscribe(Action<string> listener)
    {
        lock (Gate) Written -= listener;
    }

    /// <summary>Like <see cref="Subscribe"/> for the player events only.</summary>
    public static IReadOnlyList<string> SubscribeEvents(Action<string> listener)
    {
        lock (Gate)
        {
            EventWritten += listener;
            return EventHistory.ToList();
        }
    }

    public static void UnsubscribeEvents(Action<string> listener)
    {
        lock (Gate) EventWritten -= listener;
    }

    private static void Write(string message, bool isEvent)
    {
        var line = Stamp(message);
        Console.WriteLine(line);
        Action<string>? listeners;
        Action<string>? eventListeners = null;
        lock (Gate)
        {
            History.Enqueue(line);
            if (History.Count > HistoryLines) History.Dequeue();
            AppendToFile(line);
            listeners = Written;
            if (isEvent)
            {
                EventHistory.Enqueue(message);
                if (EventHistory.Count > EventHistoryLines) EventHistory.Dequeue();
                eventListeners = EventWritten;
            }
        }
        listeners?.Invoke(line);
        eventListeners?.Invoke(message);
    }

    private static string OlderRun(string previousPath, int run) =>
        run < FirstOlderRun
            ? previousPath
            : Path.Combine(Path.GetDirectoryName(previousPath)!,
                Path.GetFileNameWithoutExtension(previousPath) + run + Path.GetExtension(previousPath));

    private static string Stamp(string message) => $"[{DateTime.Now:HH:mm:ss.fff}] {message}";

    private static void AppendToFile(string line)
    {
        try
        {
            _file?.WriteLine(line);
        }
        catch (IOException)
        {
            _file = null;
        }
    }
}
