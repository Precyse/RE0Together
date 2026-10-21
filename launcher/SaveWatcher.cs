using System.Collections.Concurrent;

namespace CoopLauncher;

/// <summary>Host side of save sync after the join: notices saves the game writes into the save folder and reports each name
/// once it has stopped changing (a save is written in many small steps, and the game may still hold the file). When the
/// operating system drops events (its buffer overflowed) the watcher is created again and every matching file is reported as changed.</summary>
public sealed class SaveWatcher : IDisposable
{
    private const long SettleMs = 3000;

    private readonly string _saveDir;
    private readonly string _pattern;
    private readonly object _gate = new();
    private readonly ConcurrentDictionary<string, long> _lastChangeMs = new();
    private FileSystemWatcher _watcher;
    private bool _disposed;

    public SaveWatcher(string saveDir, string pattern)
    {
        _saveDir = saveDir;
        _pattern = pattern;
        _watcher = Start();
    }

    /// <summary>Marks a file as just changed, so it is reported again after the settle delay (also how a failed send is retried).</summary>
    public void Touch(string? name)
    {
        if (name != null) _lastChangeMs[name] = Environment.TickCount64;
    }

    /// <summary>The files that have not changed for the settle delay; each is returned once per change.</summary>
    public List<string> TakeSettled()
    {
        var now = Environment.TickCount64;
        var settled = _lastChangeMs.Where(p => now - p.Value >= SettleMs).Select(p => p.Key).ToList();
        foreach (var name in settled) _lastChangeMs.TryRemove(name, out _);
        return settled;
    }

    public void Dispose()
    {
        lock (_gate)
        {
            _disposed = true;
            _watcher.Dispose();
        }
    }

    private FileSystemWatcher Start()
    {
        var watcher = new FileSystemWatcher(_saveDir, _pattern)
        {
            NotifyFilter = NotifyFilters.FileName | NotifyFilters.LastWrite | NotifyFilters.Size,
        };
        watcher.Changed += (_, e) => Touch(e.Name);
        watcher.Created += (_, e) => Touch(e.Name);
        watcher.Renamed += (_, e) => Touch(e.Name);
        watcher.Error += OnError;
        watcher.EnableRaisingEvents = true;
        return watcher;
    }

    private void OnError(object sender, ErrorEventArgs e)
    {
        Log.Info($"Save sync: the save folder watcher lost events ({e.GetException().Message}), watching it again");
        lock (_gate)
        {
            if (_disposed) return;
            _watcher.Dispose();
            try
            {
                _watcher = Start();
            }
            catch (Exception ex) when (ex is IOException or ArgumentException or UnauthorizedAccessException)
            {
                Log.Info($"Save sync: could not watch {_saveDir} again ({ex.Message}), later saves are not sent");
                return;
            }
        }
        TouchAll();
    }

    private void TouchAll()
    {
        try
        {
            foreach (var path in Directory.GetFiles(_saveDir, _pattern)) Touch(Path.GetFileName(path));
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            Log.Info($"Save sync: could not list {_saveDir} ({e.Message})");
        }
    }
}
