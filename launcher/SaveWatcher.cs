using System.Collections.Concurrent;

namespace CoopLauncher;

/// <summary>Host side of save sync after the join: notices saves the game writes into the save folder and reports each name
/// once it has stopped changing (a save is written in many small steps, and the game may still hold the file).</summary>
public sealed class SaveWatcher : IDisposable
{
    private const long SettleMs = 3000;

    private readonly FileSystemWatcher _watcher;
    private readonly ConcurrentDictionary<string, long> _lastChangeMs = new();

    public SaveWatcher(string saveDir, string pattern)
    {
        _watcher = new FileSystemWatcher(saveDir, pattern)
        {
            NotifyFilter = NotifyFilters.FileName | NotifyFilters.LastWrite | NotifyFilters.Size,
        };
        _watcher.Changed += (_, e) => Touch(e.Name);
        _watcher.Created += (_, e) => Touch(e.Name);
        _watcher.Renamed += (_, e) => Touch(e.Name);
        _watcher.EnableRaisingEvents = true;
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

    public void Dispose() => _watcher.Dispose();
}
