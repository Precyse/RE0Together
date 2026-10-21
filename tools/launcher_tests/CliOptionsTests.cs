using CoopLauncher;

namespace LauncherTests;

/// <summary>CliOptions.Parse: a bad command line is named on stderr; a Steam join's lobby id is read like a pasted code.</summary>
public static class CliOptionsTests
{
    public static void Run()
    {
        var host = CliOptions.Parse(["host", "re0"]);
        Check.That(host is { Command: Command.Host, Argument: "re0", Transport: TransportKind.Steam }, "host <game>");

        var join = CliOptions.Parse(["join", "1097 7524 3712 345678"]);
        Check.That(join is { Command: Command.Join, Argument: "109775243712345678" }, "a spaced lobby code becomes plain digits");
        Check.That(CliOptions.Parse(["+connect_lobby", "steam://joinlobby/480/109775243712345678/7656"]) is { Argument: "109775243712345678" }, "a joinlobby link gives its lobby");

        var local = CliOptions.Parse(["join", "0", "--game", "re0", "--transport", "local", "--local-port", "1", "--peer-port", "2", "--no-launch"]);
        Check.That(local is { Command: Command.Join, Argument: "0", NoLaunch: true, Game: "re0" }, "a local join takes any argument");

        Check.That(Rejected(["bogus"]) == "Unknown argument: bogus", "an unknown argument is named");
        Check.That(Rejected(["host"]) == "Unknown argument: host", "a command without its value is named");
        Check.That(Rejected(["--local-port", "x"]) == "Unknown argument: --local-port x", "a malformed number is named with its flag");
        Check.That(Rejected(["--transport", "carrier-pigeon"]) == "Unknown argument: --transport carrier-pigeon", "an unknown transport is named");
        Check.That(Rejected(["join", "not a lobby"]) == "Unknown argument: not a lobby", "a bad lobby id is named");
        Check.That(Rejected(["join", "0"]) == "Unknown argument: 0", "lobby 0 is refused for Steam");
        Check.That(Rejected(["--transport", "local", "--local-port", "1"]) == "", "an incomplete local setup is refused without naming an argument");
    }

    /// <summary>The line written to stderr when the arguments are refused; null when they were accepted.</summary>
    private static string? Rejected(string[] args)
    {
        var original = Console.Error;
        var captured = new StringWriter();
        Console.SetError(captured);
        try
        {
            return CliOptions.Parse(args) == null ? captured.ToString().Trim() : null;
        }
        finally
        {
            Console.SetError(original);
        }
    }
}
