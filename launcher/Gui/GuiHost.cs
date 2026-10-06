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
        Application.SetUnhandledExceptionMode(UnhandledExceptionMode.CatchException);
        Application.ThreadException += (_, e) => Log.Info($"Error: {e.Exception.Message}");
        var app = new App(options, interactive: true);
        var closing = new ManualResetEventSlim();
        var loop = new Thread(() => RunLoop(app, closing)) { IsBackground = true, Name = "app-loop" };
        loop.Start();
        var window = new MainForm(app);
        instance.OnShowRequested(window.BringForward);
        Application.Run(window);
        closing.Set();
        app.Stop();
        loop.Join(StopTimeoutMs);
        return 0;
    }

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
                if (e.Message != lastFailure) Log.Info($"Fatal: {e.Message}");
                lastFailure = e.Message;
                app.MarkOffline();
                closing.Wait(RetryDelayMs);
            }
        }
    }
}
