namespace CoopLauncher;

/// <summary>Host side of save sync: chooses the profile's files for a peer and streams them with the shared <see cref="FileStreamer"/> (paced, resent until ACKed ok). A set the
/// guest could not take (too many names, a name that is not ASCII, a file over <see cref="SaveReceiver.MaxFileBytes"/>) is not sent
/// at all, and a missing save folder is an empty set, so the guest never waits for something that cannot arrive.</summary>
public sealed class SaveSender
{
    private const string Label = "Save sync";

    private readonly SaveSyncProfile _config;
    private readonly string _sourceDir;
    private readonly ITransport _transport;
    private readonly FileStreamer _streamer;
    private readonly HashSet<string> _logged = [];

    /// <summary>The clock (milliseconds) defaults to the system tick count; tests drive their own.</summary>
    public SaveSender(SaveSyncProfile config, string sourceDir, ITransport transport, Func<long>? clock = null)
    {
        _config = config;
        _sourceDir = sourceDir;
        _transport = transport;
        _streamer = new FileStreamer(transport, Label, clock);
    }

    public void SendTo(ulong peer)
    {
        _streamer.RemovePeer(peer);
        var names = FileNames();
        if (Unsendable(names) is { } reason)
        {
            LogOnce($"Save sync: not sending the save set to {peer}: {reason}");
            return;
        }
        if (_config.FilePattern != null)
        {
            _transport.Send(peer, FileMessages.Manifest(names));
            Log.Info($"Save sync: announced {names.Count} files to {peer}");
        }
        foreach (var name in names) Enqueue(peer, name);
    }

    /// <summary>Sends one file the host's game wrote after the join, with the manifest again (it may be a new name).
    /// False when the file could not be read (the game still holds it): ask again later.</summary>
    public bool SendChanged(ulong peer, string name)
    {
        _streamer.Remove(peer, name);
        var names = FileNames();
        if (Unsendable(names) is { } reason)
        {
            LogOnce($"Save sync: not sending {name} to {peer}: {reason}");
            return true;
        }
        if (_config.FilePattern != null) _transport.Send(peer, FileMessages.Manifest(names));
        return Enqueue(peer, name);
    }

    /// <summary>Why the guest could not take this set, or null when it can.</summary>
    private string? Unsendable(IReadOnlyList<string> names)
    {
        if (names.Count > FileMessages.MaxManifestNames) return $"{names.Count} files, at most {FileMessages.MaxManifestNames} fit the manifest";
        if (names.FirstOrDefault(name => !FileMessages.IsWireName(name)) is { } badName) return $"'{badName}' is not an ASCII name of at most {byte.MaxValue} characters";
        var tooBig = names.FirstOrDefault(name => File.Exists(Path.Combine(_sourceDir, name)) && new FileInfo(Path.Combine(_sourceDir, name)).Length > SaveReceiver.MaxFileBytes);
        return tooBig == null ? null : $"{tooBig} is over {SaveReceiver.MaxFileBytes} bytes";
    }

    private void LogOnce(string message)
    {
        if (_logged.Add(message)) Log.Info(message);
    }

    private bool Enqueue(ulong peer, string name)
    {
        var path = Path.Combine(_sourceDir, name);
        if (!File.Exists(path))
        {
            Log.Info($"Save sync: {path} not found, nothing to send for {name}");
            return true;
        }
        byte[] data;
        try
        {
            data = File.ReadAllBytes(path);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            Log.Info($"Save sync: {path} cannot be read yet ({e.Message})");
            return false;
        }
        _streamer.Enqueue(peer, name, data);
        return true;
    }

    /// <summary>The profile's fixed list, or every file in the source folder that matches the profile's pattern (none when the folder is missing).</summary>
    private List<string> FileNames()
    {
        if (_config.FilePattern is not { } pattern) return _config.SteamRemoteFiles;
        try
        {
            return Directory.GetFiles(_sourceDir, pattern).Select(Path.GetFileName).OfType<string>().Order().ToList();
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            LogOnce($"Save sync: {_sourceDir} cannot be read ({e.Message}), sending an empty set");
            return [];
        }
    }

    public void OnAck(ulong peer, Frame frame) => _streamer.OnAck(peer, frame);

    public void Pump() => _streamer.Pump();
}
