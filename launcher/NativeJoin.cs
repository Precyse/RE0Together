namespace CoopLauncher;

/// <summary>
/// The launcher's side of a rendezvous game (SessionKind.Rendezvous): the Steam lobby says who hosts, and the game's mod
/// connects itself. This writes the game's join file (role, token = the lobby id, password = the lobby's secret, name = the
/// player's Steam name as <see cref="PlayerName"/> shapes it) and passes the status line the mod writes on to the other
/// lobby members. The role is fixed when the session starts: a host that leaves the lobby does not turn a guest into a host.
/// The file is removed when the session ends.
/// </summary>
public sealed class NativeJoin : IDisposable
{
    private const long PollIntervalMs = 2000;
    private const string StatePrefix = "state=";
    private const string DetailKey = " detail=";

    private readonly string _joinPath;
    private readonly string _statusPath;
    private readonly ulong _localId;
    private readonly ulong _hostId;
    private ILobby _lobby;
    private bool _hostLeftLogged;
    private string _written = string.Empty;
    private string _status = string.Empty;
    private long _nextPollMs;

    private NativeJoin(string joinPath, string statusPath, ILobby lobby, ulong localId)
    {
        _joinPath = joinPath;
        _statusPath = statusPath;
        _lobby = lobby;
        _localId = localId;
        _hostId = lobby.OwnerId;
    }

    /// <summary>Null when the profile names no join and status file. A status left by an earlier run is removed.</summary>
    public static NativeJoin? Create(GameProfile profile, string gameDir, ILobby lobby, ulong localId)
    {
        if (profile.JoinFile is not { } join || profile.StatusFile is not { } status)
        {
            Log.Info($"{profile.Name} has no join file in its profile, the game will not be told who hosts");
            return null;
        }
        var nativeJoin = new NativeJoin(Path.Combine(gameDir, join), Path.Combine(gameDir, status), lobby, localId);
        TryDelete(nativeJoin._statusPath);
        return nativeJoin;
    }

    /// <summary>The status line as a player reads it: a sentence, with what to do when there is something to do. A state this
    /// launcher does not know is shown as the mod wrote it.</summary>
    public static string Readable(string line)
    {
        var detailAt = line.IndexOf(DetailKey, StringComparison.Ordinal);
        var state = (detailAt < 0 ? line : line[..detailAt]).Replace(StatePrefix, string.Empty);
        var detail = detailAt < 0 ? string.Empty : line[(detailAt + DetailKey.Length)..];
        return state switch
        {
            "advertising" => "Hosting, waiting for players to join",
            "in_session" => int.TryParse(detail, out var members) ? $"In the session with {members} {(members == 1 ? "player" : "players")}" : "In the session",
            "wrong_mode" => "Wrong menu: host from Co-op > LAN, not Online",
            "waiting" => WaitingWords(detail),
            "login_needed" => "Not signed in: sign in to an online profile in the game's Co-op > Online menu",
            "searching" => int.TryParse(detail, out var listed) && listed > 0 ? $"Looking for the host's game ({listed} listed)" : "Looking for the host's game",
            "found" => "Found the host's game, joining",
            "dialog" => "Answer the game's dialog (pick a save or New Game) to continue",
            "joining" => "Joining the host's game",
            "joined" => "In the host's game",
            "failed" => FailedWords(detail),
            "stopped" => "Co-op stopped",
            _ => $"{state.Replace('_', ' ')} {detail}".Trim(),
        };
    }

    private static string WaitingWords(string detail) => detail switch
    {
        "link" => "Waiting for the connection to the host",
        "front end" => "Waiting for the game's main menu (close any open menu or dialog)",
        "login" => "Signing in to the online service",
        _ => $"Waiting {detail}".Trim(),
    };

    private static string FailedWords(string detail) => detail switch
    {
        "version" => "Could not join: the host's Saints Row 2 or mod version differs from yours",
        "password" => "Could not join: the host refused the session password, leave and join the lobby again",
        "host_address" => "Could not join: the game found in the list is not the host's, leave and join the lobby again",
        "no join" => "Could not join after several tries, leave and join the lobby again",
        _ => $"Could not join {detail}".Trim(),
    };

    /// <summary>A new lobby replaced the failed one for the same session: its id goes into the join file and the status line
    /// is published to it again.</summary>
    public void Rebind(ILobby lobby)
    {
        _lobby = lobby;
        _status = string.Empty;
        _hostLeftLogged = false;
    }

    public void Pump()
    {
        var now = Environment.TickCount64;
        if (now < _nextPollMs) return;
        _nextPollMs = now + PollIntervalMs;
        LogHostLeft();
        WriteJoinFile();
        PublishStatus();
    }

    public void Dispose() => TryDelete(_joinPath);

    private void LogHostLeft()
    {
        if (_hostLeftLogged || _lobby.OwnerId == _hostId) return;
        _hostLeftLogged = true;
        Log.Info("The host left the lobby");
    }

    /// <summary>Written when the lobby has its id and secret, and again whenever its content changes; atomically, so the
    /// mod never reads half of it.</summary>
    private void WriteJoinFile()
    {
        if (_lobby.Id == 0 || _lobby.JoinSecret.Length == 0) return;
        var role = _hostId == _localId ? "host" : "guest";
        var name = PlayerName.ForLocal(_localId, _lobby.Members);
        var nameLine = name.Length > 0 ? $"name={name}\n" : string.Empty;
        var content = $"role={role}\ntoken={_lobby.Id}\npassword={_lobby.JoinSecret}\n{nameLine}";
        if (content == _written) return;
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(_joinPath)!);
            var temp = _joinPath + ".tmp";
            File.WriteAllText(temp, content);
            File.Move(temp, _joinPath, overwrite: true);
            _written = content;
            Log.Info($"Join file written ({role}, lobby {_lobby.Id}, name \"{name}\")");
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            Log.Info($"Could not write the join file: {e.Message}");
        }
    }

    private void PublishStatus()
    {
        string line;
        try
        {
            using var stream = new FileStream(_statusPath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
            line = new StreamReader(stream).ReadToEnd().Trim();
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            return;
        }
        if (line == _status) return;
        _status = line;
        _lobby.SetMemberData(MemberKey.GameStatus, line);
    }

    private static void TryDelete(string path)
    {
        try
        {
            File.Delete(path);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            Log.Info($"Could not remove {Path.GetFileName(path)}: {e.Message}");
        }
    }
}
