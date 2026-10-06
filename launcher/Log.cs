namespace CoopLauncher;

public static class Log
{
    private const int HistoryLines = 500;
    private static readonly object Gate = new();
    private static readonly Queue<string> History = new();
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
            listeners = Written;
        }
        listeners?.Invoke(line);
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
}
