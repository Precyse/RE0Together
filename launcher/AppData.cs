namespace CoopLauncher;

/// <summary>The launcher's per-user folder (%AppData%\CoopLauncher, or the folder named by COOP_LAUNCHER_DATA, which tests
/// and the UI harness point at a temp folder): its settings and its logs.</summary>
public static class AppData
{
    /// <summary>The timestamp every file the launcher names by time uses (archives, reports, peer logs).</summary>
    public const string StampFormat = "yyyyMMdd-HHmmss";

    public const string DataRootVariable = "COOP_LAUNCHER_DATA";

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

    private static string Root =>
        Environment.GetEnvironmentVariable(DataRootVariable) is { Length: > 0 } custom
            ? custom
            : Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), FolderName);
}
