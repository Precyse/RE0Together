using CoopLauncher;
using CoopLauncher.Gui;

internal static class Program
{
    [STAThread]
    private static int Main(string[] args)
    {
        if (args.Length != 0) ParentConsole.Attach();
        var options = CliOptions.Parse(args);
        if (options == null)
        {
            Console.Error.WriteLine(CliOptions.Usage);
            return 2;
        }
        return args.Length != 0 ? RunCli(args, options) : RunWindow(args, options);  // tools and tests run several command-line launchers side by side
    }

    private static int RunWindow(string[] args, CliOptions options)
    {
        using var instance = SingleInstance.Acquire();
        if (instance == null) return 0;
        return UpdateInstalled(args) ? 0 : GuiHost.Run(options, instance);
    }

    private static int RunCli(string[] args, CliOptions options)
    {
        if (UpdateInstalled(args)) return 0;
        var app = new App(options);
        Console.CancelKeyPress += (_, e) =>
        {
            e.Cancel = true;
            app.Stop();
        };
        try
        {
            return app.Run();
        }
        catch (Exception e)
        {
            Log.Info($"Fatal: {e.Message}");
            return 1;
        }
    }

    /// <summary>True when a newer build was installed and started: this one must exit. Skipped when the settings turn the start-up check off.</summary>
    private static bool UpdateInstalled(string[] args) => AppSettings.Current.CheckForUpdates && Updater.TryInstall(args);
}
