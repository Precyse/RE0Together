using CoopLauncher.Web;
using Steamworks;

namespace CoopLauncher.Gui;

/// <summary>Runs the App loop on a background thread and the window on the calling (STA) thread. A loop that cannot
/// start (Steam not running) is retried while the window stays open.</summary>
public static class GuiHost
{
    private const int StopTimeoutMs = 5000;
    private const int RetryDelayMs = 3000;

    public static int Run(CliOptions options, SingleInstance instance)
    {
        Log.WriteToFile(AppData.LogFile, AppData.PreviousLogFile);
        ApplicationConfiguration.Initialize();
        if (!UiAvailable()) return 1;
        Application.SetUnhandledExceptionMode(UnhandledExceptionMode.CatchException);
        Application.ThreadException += (_, e) => Log.Info($"Error: {e.Exception.Message}");
        var app = new App(options, interactive: true);
        var closing = new ManualResetEventSlim();
        var loop = new Thread(() => RunLoop(app, closing)) { IsBackground = true, Name = "app-loop" };
        loop.Start();
        var (window, bringForward) = CreateWindow(app);
        instance.OnShowRequested(bringForward);
        Application.Run(window);
        closing.Set();
        app.Stop();
        loop.Join(StopTimeoutMs);
        return 0;
    }

#if CLASSIC_UI
    private static bool UiAvailable() => true;

    private static (Form Window, Action BringForward) CreateWindow(App app)
    {
        var window = new MainForm(app);
        return (window, window.BringForward);
    }
#else
    private static bool UiAvailable() => WebRuntime.EnsureOrExplain();

    private static (Form Window, Action BringForward) CreateWindow(App app)
    {
        var window = new WebShell(app);
        return (window, window.BringForward);
    }
#endif

    private static void RunLoop(App app, ManualResetEventSlim closing)
    {
        string? lastFailure = null;
        while (!closing.IsSet)
        {
            try
            {
                app.Run();
                return;
            }
            catch (Exception e)
            {
                var detail = e.ToString();
                if (detail != lastFailure)
                {
                    Log.FileOnly($"Fatal: {detail}");
                    Log.Info($"Fatal: {e.Message}");
                }
                lastFailure = detail;
                app.MarkOffline(SteamIsMissing(e) ? OfflineReason.SteamNotRunning : OfflineReason.None);
                closing.Wait(RetryDelayMs);
            }
        }
    }

    /// <summary>Steam is not running (it could not be reached at start, or it went away).</summary>
    private static bool SteamIsMissing(Exception e) =>
        e is SteamLostException || (e is SteamUnavailableException { Result: ESteamAPIInitResult.k_ESteamAPIInitResult_NoSteamClient });
}
