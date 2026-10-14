namespace CoopLauncher;

/// <summary>
/// The launcher's side of a rendezvous game (SessionKind.Rendezvous): the Steam lobby says who hosts, and the game's mod
/// connects itself. This writes the game's join file (role, token = the lobby id, password = the lobby's secret) and passes
/// the status line the mod writes on to the other lobby members. The file is removed when the session ends.
/// </summary>
public sealed class NativeJoin : IDisposable
{
    private const long PollIntervalMs = 2000;
    private const string StatePrefix = "state=";
    private const string DetailKey = " detail=";

    private readonly string _joinPath;
    private readonly string _statusPath;
    private readonly ILobby _lobby;
    private readonly ulong _localId;
    private string _written = string.Empty;
    private string _status = string.Empty;
    private long _nextPollMs;

    private NativeJoin(string joinPath, string statusPath, ILobby lobby, ulong localId)
    {
        _joinPath = joinPath;
        _statusPath = statusPath;
        _lobby = lobby;
        _localId = localId;
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

    /// <summary>The status line as a player reads it: "searching 3", "login needed".</summary>
    public static string Readable(string line)
    {
        var detailAt = line.IndexOf(DetailKey, StringComparison.Ordinal);
        var state = (detailAt < 0 ? line : line[..detailAt]).Replace(StatePrefix, string.Empty).Replace('_', ' ');
        var detail = detailAt < 0 ? string.Empty : line[(detailAt + DetailKey.Length)..];
        return $"{state} {detail}".Trim();
    }

    public void Pump()
    {
        var now = Environment.TickCount64;
        if (now < _nextPollMs) return;
        _nextPollMs = now + PollIntervalMs;
        WriteJoinFile();
        PublishStatus();
    }

    public void Dispose() => TryDelete(_joinPath);

    /// <summary>Written when the lobby has its id and secret, and again whenever its content changes; atomically, so the
    /// mod never reads half of it.</summary>
    private void WriteJoinFile()
    {
        if (_lobby.Id == 0 || _lobby.JoinSecret.Length == 0) return;
        var role = _lobby.OwnerId == _localId ? "host" : "guest";
        var content = $"role={role}\ntoken={_lobby.Id}\npassword={_lobby.JoinSecret}\n";
        if (content == _written) return;
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(_joinPath)!);
            var temp = _joinPath + ".tmp";
            File.WriteAllText(temp, content);
            File.Move(temp, _joinPath, overwrite: true);
            _written = content;
            Log.Info($"Join file written ({role}, lobby {_lobby.Id})");
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
        _lobby.SetMemberStatus(line);
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
