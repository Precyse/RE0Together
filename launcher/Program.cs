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
        return args.Length != 0 ? RunCli(args, options) : RunWindow(options);  // tools and tests run several command-line launchers side by side
    }

    /// <summary>The window opens first; a newer build is staged in the background and installed by the restart the player asks for.</summary>
    private static int RunWindow(CliOptions options)
    {
        int result;
        using (var instance = SingleInstance.Acquire())
        {
            if (instance == null) return 0;
            result = GuiHost.Run(options, instance);
        }
        if (Updater.RestartRequested) Updater.RelaunchStaged();
        return result;
    }

    private static int RunCli(string[] args, CliOptions options)
    {
        using var cliRun = new Mutex(initiallyOwned: false, InstanceGuard.CliMutexName(Environment.ProcessId));  // no window start closes this run
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
            Log.Info($"Fatal: {e}");
            return 1;
        }
    }

    /// <summary>True when a newer build was installed and started: this one must exit. Skipped when the settings turn the start-up check off.</summary>
    private static bool UpdateInstalled(string[] args) => AppSettings.Current.CheckForUpdates && Updater.TryInstall(args);
}
