using System.Runtime.InteropServices;

namespace CoopLauncher.Gui;

/// <summary>Runs the App loop on a background thread and the window on the calling (STA) thread.</summary>
public static class GuiHost
{
    private const int StopTimeoutMs = 5000;

    [DllImport("kernel32.dll")]
    private static extern bool FreeConsole();

    public static int Run(CliOptions options)
    {
        FreeConsole();
        ApplicationConfiguration.Initialize();
        var app = new App(options, interactive: true);
        var loop = new Thread(() => RunLoop(app)) { IsBackground = true, Name = "app-loop" };
        loop.Start();
        Application.Run(new MainForm(app));
        app.Stop();
        loop.Join(StopTimeoutMs);
        return 0;
    }

    private static void RunLoop(App app)
    {
        try
        {
            app.Run();
        }
        catch (Exception e)
        {
            Log.Info($"Fatal: {e.Message}");
        }
    }
}
