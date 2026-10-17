namespace CoopLauncher;

/// <summary>Who is held, who came back and who is gone after one membership check.</summary>
public sealed record GraceVerdict(IReadOnlyList<ulong> Held, IReadOnlyList<ulong> Returned, IReadOnlyList<ulong> Expired);

/// <summary>
/// A session member that disappears from the lobby is held for <see cref="PeerGraceMs"/> before it counts as gone, so a
/// network hiccup resyncs the peer instead of resetting the session. Pure: the caller supplies the clock.
/// </summary>
public sealed class MemberGrace
{
    public const long PeerGraceMs = 10_000;

    private readonly Dictionary<ulong, long> _missingSince = new();

    /// <summary>Checks the slot members against the ids now present. A missing member is held until the grace is over, then
    /// expired (and forgotten); one that reappears while held is returned.</summary>
    public GraceVerdict Update(IEnumerable<ulong> slotMembers, ISet<ulong> present, long nowMs)
    {
        var members = slotMembers.ToList();
        foreach (var id in _missingSince.Keys.Except(members).ToList()) _missingSince.Remove(id);
        List<ulong> held = [], returned = [], expired = [];
        foreach (var id in members)
        {
            if (present.Contains(id))
            {
                if (_missingSince.Remove(id)) returned.Add(id);
                continue;
            }
            var since = _missingSince.TryAdd(id, nowMs) ? nowMs : _missingSince[id];
            if (nowMs - since >= PeerGraceMs)
            {
                _missingSince.Remove(id);
                expired.Add(id);
            }
            else held.Add(id);
        }
        return new GraceVerdict(held, returned, expired);
    }

    /// <summary>True when a held member's grace is over, so the membership needs another check even if the lobby did not change.</summary>
    public bool ExpiryDue(long nowMs) => _missingSince.Values.Any(since => nowMs - since >= PeerGraceMs);

    /// <summary>Forgets every hold (the lobby was replaced).</summary>
    public void Clear() => _missingSince.Clear();
}
