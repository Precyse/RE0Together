namespace CoopLauncher;

public static class Log
{
    /// <summary>Raised with each formatted line, on the calling thread.</summary>
    public static event Action<string>? Written;

    public static void Info(string message)
    {
        var line = $"[{DateTime.Now:HH:mm:ss.fff}] {message}";
        Console.WriteLine(line);
        Written?.Invoke(line);
    }
}
