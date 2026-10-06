namespace CoopLauncher;

public static class Log
{
    private const int HistoryLines = 500;
    private static readonly object Gate = new();
    private static readonly Queue<string> History = new();
    private static StreamWriter? _file;
    private static event Action<string>? Written;

    public static void Info(string message)
    {
        var line = $"[{DateTime.Now:HH:mm:ss.fff}] {message}";
        Console.WriteLine(line);
        Action<string>? listeners;
        lock (Gate)
        {
            History.Enqueue(line);
            if (History.Count > HistoryLines) History.Dequeue();
            AppendToFile(line);
            listeners = Written;
        }
        listeners?.Invoke(line);
    }

    /// <summary>Also writes every line to the file from now on (the lines so far first). The previous run's file is kept
    /// as <paramref name="previousPath"/>. A file that cannot be written is skipped: logging must never stop the launcher.</summary>
    public static void WriteToFile(string path, string previousPath)
    {
        lock (Gate)
        {
            try
            {
                Directory.CreateDirectory(Path.GetDirectoryName(path)!);
                if (File.Exists(path)) File.Move(path, previousPath, overwrite: true);
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
