using CoopLauncher;
using CoopLauncher.Gui;

internal static class Program
{
    [STAThread]
    private static int Main(string[] args)
    {
        var options = CliOptions.Parse(args);
        if (options == null)
        {
            Console.Error.WriteLine(CliOptions.Usage);
            return 2;
        }
        if (Updater.TryInstall(args)) return 0;
        return args.Length == 0 ? GuiHost.Run(options) : RunCli(options);
    }

    private static int RunCli(CliOptions options)
    {
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
}
