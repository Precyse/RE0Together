using System.Globalization;

namespace CoopLauncher;

/// <summary>
/// Newest-wins holding queue for a game's fresh frame types (profile <c>freshTypes</c>): an unreliable frame of such a
/// type waits here until the next flush, and a newer frame of the same (peer, type) replaces it, so a burst delivered
/// together (after a stall) sends only the latest state. Reliable frames and every other type are never held. Pure.
/// </summary>
public sealed class FreshQueue
{
    private readonly HashSet<ushort> _types;
    private readonly Dictionary<(ulong Peer, ushort Type), Frame> _pending = new();
    private readonly List<(ulong Peer, ushort Type)> _order = new();

    public FreshQueue(IEnumerable<ushort> types) => _types = types.ToHashSet();

    public int TypeCount => _types.Count;

    /// <summary>Frames dropped because a newer one of the same (peer, type) arrived before the flush.</summary>
    public long Replaced { get; private set; }

    /// <summary>The queue for a profile, or null when it lists no fresh types (the game then sends as it always did).</summary>
    public static FreshQueue? ForProfile(GameProfile profile)
    {
        var types = ParseTypes(profile.FreshTypes);
        return types.Count == 0 ? null : new FreshQueue(types);
    }

    /// <summary>Hex names such as "0x0140". A type below the first game type is refused: only game frames may be fresh.</summary>
    public static IReadOnlyList<ushort> ParseTypes(IEnumerable<string>? names)
    {
        var types = new List<ushort>();
        foreach (var name in names ?? [])
        {
            var digits = name.StartsWith("0x", StringComparison.OrdinalIgnoreCase) ? name[2..] : name;
            if (!ushort.TryParse(digits, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out var type) || type < Msg.FirstGameType)
                throw new InvalidDataException($"Bad freshTypes entry '{name}': expected a game frame type from 0x{Msg.FirstGameType:x4}");
            types.Add(type);
        }
        return types;
    }

    public bool Accepts(Frame frame) => (frame.Flags & Framing.FlagReliable) == 0 && _types.Contains(frame.Type);

    public void Enqueue(ulong peerId, Frame frame)
    {
        var key = (peerId, frame.Type);
        if (!_pending.TryAdd(key, frame))
        {
            _pending[key] = frame;
            Replaced++;
            return;
        }
        _order.Add(key);
    }

    /// <summary>Every held frame, oldest first-queued first, and empties the queue.</summary>
    public IReadOnlyList<(ulong Peer, Frame Frame)> Drain()
    {
        if (_order.Count == 0) return [];
        var drained = _order.Select(key => (key.Peer, _pending[key])).ToList();
        _pending.Clear();
        _order.Clear();
        return drained;
    }
}
