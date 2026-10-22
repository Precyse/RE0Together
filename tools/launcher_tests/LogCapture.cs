using CoopLauncher;

namespace LauncherTests;

/// <summary>The log lines an action writes.</summary>
internal static class LogCapture
{
    public static List<string> During(Action action)
    {
        var lines = new List<string>();
        Action<string> collect = lines.Add;
        Log.Subscribe(collect);
        try
        {
            action();
        }
        finally
        {
            Log.Unsubscribe(collect);
        }
        return lines;
    }
}
