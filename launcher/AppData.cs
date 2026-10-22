namespace CoopLauncher;

/// <summary>The launcher's per-user folder (%AppData%\CoopLauncher, or the folder named by COOP_LAUNCHER_DATA, which tests
/// point at a temp folder): its settings, its logs, the pictures it took from games and the web view's data.</summary>
public static class AppData
{
    /// <summary>The timestamp every file the launcher names by time uses (archives, reports, peer logs).</summary>
    public const string StampFormat = "yyyyMMdd-HHmmss";

    public const string DataRootVariable = "COOP_LAUNCHER_DATA";

    private const string FolderName = "CoopLauncher";
    private const string SettingsFileName = "settings.json";
    private const string LogsFolderName = "logs";
    private const string ArtFolderName = "art";
    private const string WebViewFolderName = "webview2";
    private const string LogFileName = "launcher.log";
    private const string PreviousLogFileName = "launcher.prev.log";

    public static string SettingsFile => Path.Combine(Root, SettingsFileName);

    public static string LogsDir => Path.Combine(Root, LogsFolderName);

    public static string LogFile => Path.Combine(LogsDir, LogFileName);

    /// <summary>Pictures taken from a game's files, one folder per game.</summary>
    public static string ArtDir => Path.Combine(Root, ArtFolderName);

    /// <summary>The WebView2 user data folder (cache, cookies and the browser's own files of the window's page).</summary>
    public static string WebViewDir => Path.Combine(Root, WebViewFolderName);

    /// <summary>The log of the run before this one.</summary>
    public static string PreviousLogFile => Path.Combine(LogsDir, PreviousLogFileName);

    private static string Root =>
        Environment.GetEnvironmentVariable(DataRootVariable) is { Length: > 0 } custom
            ? custom
            : Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), FolderName);
}
