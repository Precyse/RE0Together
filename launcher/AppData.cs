namespace CoopLauncher;

/// <summary>The launcher's per-user folder in %AppData%: its settings and its own log.</summary>
public static class AppData
{
    private const string FolderName = "CoopLauncher";
    private const string SettingsFileName = "settings.json";
    private const string LogsFolderName = "logs";
    private const string LogFileName = "launcher.log";
    private const string PreviousLogFileName = "launcher.prev.log";

    public static string SettingsFile => Path.Combine(Root, SettingsFileName);

    public static string LogsDir => Path.Combine(Root, LogsFolderName);

    public static string LogFile => Path.Combine(LogsDir, LogFileName);

    /// <summary>The log of the run before this one.</summary>
    public static string PreviousLogFile => Path.Combine(LogsDir, PreviousLogFileName);

    private static string Root => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), FolderName);
}
