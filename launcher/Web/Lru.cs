namespace CoopLauncher.Web;

/// <summary>A small least-recently-used cache, safe to use from the UI thread and the loading threads. The capacity is a named
/// constant at each use.</summary>
internal sealed class Lru<TKey, TValue> where TKey : notnull
{
    private readonly int _capacity;
    private readonly Dictionary<TKey, LinkedListNode<KeyValuePair<TKey, TValue>>> _nodes = new();
    private readonly LinkedList<KeyValuePair<TKey, TValue>> _recent = new();   // most recently used first
    private readonly object _gate = new();

    public Lru(int capacity)
    {
        ArgumentOutOfRangeException.ThrowIfLessThan(capacity, 1);
        _capacity = capacity;
    }

    public void Put(TKey key, TValue value)
    {
        lock (_gate)
        {
            if (_nodes.TryGetValue(key, out var existing)) _recent.Remove(existing);
            _nodes[key] = _recent.AddFirst(new KeyValuePair<TKey, TValue>(key, value));
            while (_nodes.Count > _capacity)
            {
                _nodes.Remove(_recent.Last!.Value.Key);
                _recent.RemoveLast();
            }
        }
    }

    public bool TryGet(TKey key, out TValue? value)
    {
        lock (_gate)
        {
            if (!_nodes.TryGetValue(key, out var node))
            {
                value = default;
                return false;
            }
            _recent.Remove(node);
            _recent.AddFirst(node);
            value = node.Value.Value;
            return true;
        }
    }

    public bool Contains(TKey key)
    {
        lock (_gate) return _nodes.ContainsKey(key);
    }

    public void Clear()
    {
        lock (_gate)
        {
            _nodes.Clear();
            _recent.Clear();
        }
    }
}
