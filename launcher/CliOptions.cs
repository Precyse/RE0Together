namespace CoopLauncher;

public enum TransportKind { Steam, Local }

public enum Command { WaitForInvite, Host, Join }

public sealed record CliOptions(
    Command Command, string? Argument, TransportKind Transport, int LocalPort, int PeerPort, int BridgePort, bool NoLaunch, string? Game,
    string? SaveSource, string? GameDir)
{
    public const string Usage = """
        Usage:
          coop-launcher host <gameId>
          coop-launcher join <lobbyId>
          coop-launcher +connect_lobby <lobbyId>
          coop-launcher                              (wait for a Steam overlay invite)
        Flags:
          --transport steam|local   default steam
          --local-port N --peer-port M   required with --transport local
          --bridge-port N           loopback port for the adapter (default: profile port; needed for two launchers on one PC)
          --game <id>               game profile for a local join (default: the only profile)
          --save-source <dir>       host: folder holding the save files (default: Steam userdata remote folder)
          --game-dir <dir>          game folder (default: resolved through Steam)
          --no-launch               do not start the game
        """;

    public static CliOptions? Parse(string[] args)
    {
        var command = Command.WaitForInvite;
        string? argument = null, game = null, saveSource = null, gameDir = null;
        var transport = TransportKind.Steam;
        int localPort = 0, peerPort = 0, bridgePort = 0;
        var noLaunch = false;

        for (var i = 0; i < args.Length; i++)
        {
            switch (args[i])
            {
                case "host" when i + 1 < args.Length:
                    (command, argument) = (Command.Host, args[++i]);
                    break;
                case "join" or "+connect_lobby" when i + 1 < args.Length:
                    (command, argument) = (Command.Join, args[++i]);
                    break;
                case "--transport" when i + 1 < args.Length:
                    if (!Enum.TryParse(args[++i], ignoreCase: true, out transport)) return null;
                    break;
                case "--local-port" when i + 1 < args.Length:
                    if (!int.TryParse(args[++i], out localPort)) return null;
                    break;
                case "--peer-port" when i + 1 < args.Length:
                    if (!int.TryParse(args[++i], out peerPort)) return null;
                    break;
                case "--bridge-port" when i + 1 < args.Length:
                    if (!int.TryParse(args[++i], out bridgePort)) return null;
                    break;
                case "--game" when i + 1 < args.Length:
                    game = args[++i];
                    break;
                case "--save-source" when i + 1 < args.Length:
                    saveSource = args[++i];
                    break;
                case "--game-dir" when i + 1 < args.Length:
                    gameDir = args[++i];
                    break;
                case "--no-launch":
                    noLaunch = true;
                    break;
                default:
                    return null;
            }
        }

        var localIncomplete = transport == TransportKind.Local && (localPort == 0 || peerPort == 0 || command == Command.WaitForInvite);
        return localIncomplete ? null : new CliOptions(command, argument, transport, localPort, peerPort, bridgePort, noLaunch, game, saveSource, gameDir);
    }
}
