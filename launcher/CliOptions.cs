namespace CoopLauncher;

public enum TransportKind { Steam, Local }

public enum Command { WaitForInvite, Host, Join }

public sealed record CliOptions(
    Command Command, string? Argument, TransportKind Transport, int LocalPort, int PeerPort, int BridgePort, bool NoLaunch, string? Game,
    string? SaveSource, string? GameDir, string? HostOptions)
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
          --host-options k=v;k=v    local join: the host's option values (a Steam lobby carries them)
          --no-launch               do not start the game
        """;

    /// <summary>Null for a command line that cannot be used; the offending argument is named on stderr first (the caller then
    /// prints <see cref="Usage"/>). A Steam join's lobby id is read with <see cref="LobbyCode"/> and kept as plain digits.</summary>
    public static CliOptions? Parse(string[] args)
    {
        var command = Command.WaitForInvite;
        string? argument = null, game = null, saveSource = null, gameDir = null, hostOptions = null;
        var transport = TransportKind.Steam;
        int localPort = 0, peerPort = 0, bridgePort = 0;
        var noLaunch = false;

        for (var i = 0; i < args.Length; i++)
        {
            var flag = args[i];
            var value = i + 1 < args.Length ? args[i + 1] : null;
            switch (flag)
            {
                case "host" when value != null:
                    (command, argument) = (Command.Host, value);
                    break;
                case "join" or "+connect_lobby" when value != null:
                    (command, argument) = (Command.Join, value);
                    break;
                case "--transport" when value != null:
                    if (!Enum.TryParse(value, ignoreCase: true, out transport)) return Reject($"{flag} {value}");
                    break;
                case "--local-port" when value != null:
                    if (!int.TryParse(value, out localPort)) return Reject($"{flag} {value}");
                    break;
                case "--peer-port" when value != null:
                    if (!int.TryParse(value, out peerPort)) return Reject($"{flag} {value}");
                    break;
                case "--bridge-port" when value != null:
                    if (!int.TryParse(value, out bridgePort)) return Reject($"{flag} {value}");
                    break;
                case "--game" when value != null:
                    game = value;
                    break;
                case "--save-source" when value != null:
                    saveSource = value;
                    break;
                case "--game-dir" when value != null:
                    gameDir = value;
                    break;
                case "--host-options" when value != null:
                    hostOptions = value;
                    break;
                case "--no-launch":
                    noLaunch = true;
                    continue;
                default:
                    return Reject(flag);
            }
            i++;
        }

        if (command == Command.Join && transport == TransportKind.Steam)
        {
            if (!LobbyCode.TryParse(argument, out var lobbyId)) return Reject(argument!);
            argument = lobbyId.ToString();
        }
        var localIncomplete = transport == TransportKind.Local && (localPort == 0 || peerPort == 0 || command == Command.WaitForInvite);
        return localIncomplete ? null : new CliOptions(command, argument, transport, localPort, peerPort, bridgePort, noLaunch, game, saveSource, gameDir, hostOptions);
    }

    private static CliOptions? Reject(string argument)
    {
        Console.Error.WriteLine($"Unknown argument: {argument}");
        return null;
    }
}
