using CoopLauncher;

namespace LauncherTests;

/// <summary>MemberGrace: a hiccup under 10 s resyncs the member (returned), over 10 s it is gone (expired); the host gets
/// its own, longer grace.</summary>
public static class MemberGraceTests
{
    private const ulong Host = 1;
    private const ulong Guest = 2;
    private static readonly ulong[] Both = [Host, Guest];

    private static long Peers(ulong id) => MemberGrace.PeerGraceMs;

    private static long HostFirst(ulong id) => id == Host ? MemberGrace.HostGraceMs : MemberGrace.PeerGraceMs;

    private static HashSet<ulong> Present(params ulong[] ids) => ids.ToHashSet();

    public static void Run()
    {
        Hiccup();
        GoneAfterGrace();
        ExpiryDue();
        HostAndTracking();
        HostGrace();
    }

    private static void Hiccup()
    {
        var grace = new MemberGrace();
        var all = grace.Update(Both, Present(Host, Guest), 0, Peers);
        Check.That(all.Held.Count + all.Returned.Count + all.Expired.Count == 0, "everyone present: nothing to do");

        var gone = grace.Update(Both, Present(Host), 1000, Peers);
        Check.That(gone.Held.SequenceEqual([Guest]) && gone.Expired.Count == 0 && gone.Returned.Count == 0, "missing: held");
        var still = grace.Update(Both, Present(Host), 5000, Peers);
        Check.That(still.Held.SequenceEqual([Guest]) && still.Expired.Count == 0, "still missing inside the grace: still held");

        var back = grace.Update(Both, Present(Host, Guest), 9000, Peers);
        Check.That(back.Returned.SequenceEqual([Guest]) && back.Expired.Count == 0 && back.Held.Count == 0,
            "back inside the grace: returned (resync, not reset)");
        var later = grace.Update(Both, Present(Host, Guest), 9100, Peers);
        Check.That(later.Returned.Count == 0, "returned only once");
    }

    private static void GoneAfterGrace()
    {
        var grace = new MemberGrace();
        grace.Update(Both, Present(Host), 0, Peers);
        var almost = grace.Update(Both, Present(Host), MemberGrace.PeerGraceMs - 1, Peers);
        Check.That(almost.Held.SequenceEqual([Guest]) && almost.Expired.Count == 0, "1 ms before the grace is over: held");
        var over = grace.Update(Both, Present(Host), MemberGrace.PeerGraceMs, Peers);
        Check.That(over.Expired.SequenceEqual([Guest]) && over.Held.Count == 0, "grace over: expired (gone)");
        var after = grace.Update([Host], Present(Host), MemberGrace.PeerGraceMs + 1, Peers);
        Check.That(after.Expired.Count == 0 && after.Held.Count == 0, "an expired member is forgotten once the slot is dropped");
        var rejoin = grace.Update([Host], Present(Host, Guest), MemberGrace.PeerGraceMs + 2, Peers);
        Check.That(rejoin.Returned.Count == 0, "a member back after expiry is not a return (it is a new join)");
    }

    private static void ExpiryDue()
    {
        var grace = new MemberGrace();
        Check.That(!grace.ExpiryDue(1_000_000, Peers), "nobody held: nothing due");
        grace.Update(Both, Present(Host), 100, Peers);
        Check.That(!grace.ExpiryDue(100 + MemberGrace.PeerGraceMs - 1, Peers), "not due inside the grace");
        Check.That(grace.ExpiryDue(100 + MemberGrace.PeerGraceMs, Peers), "due when the grace is over");
        grace.Update(Both, Present(Host), 100 + MemberGrace.PeerGraceMs, Peers);
        Check.That(!grace.ExpiryDue(100 + MemberGrace.PeerGraceMs, Peers), "not due again once expired");
    }

    private static void HostAndTracking()
    {
        var grace = new MemberGrace();
        grace.Update(Both, Present(Guest), 0, Peers);
        var over = grace.Update(Both, Present(Guest), MemberGrace.PeerGraceMs, Peers);
        Check.That(over.Expired.SequenceEqual([Host]), "with one grace for all, the host expires like any member");

        var tracked = new MemberGrace();
        tracked.Update(Both, Present(Host), 0, Peers);
        tracked.Update([Host], Present(Host), 50, Peers);
        Check.That(!tracked.ExpiryDue(MemberGrace.PeerGraceMs + 100, Peers), "a member whose slot was dropped is no longer tracked");

        var cleared = new MemberGrace();
        cleared.Update(Both, Present(Host), 0, Peers);
        cleared.Clear();
        Check.That(!cleared.ExpiryDue(MemberGrace.PeerGraceMs + 1, Peers), "Clear forgets every hold");
    }

    private static void HostGrace()
    {
        var grace = new MemberGrace();
        grace.Update(Both, Present(Guest), 0, HostFirst);
        var afterPeerGrace = grace.Update(Both, Present(Guest), MemberGrace.PeerGraceMs, HostFirst);
        Check.That(afterPeerGrace.Held.SequenceEqual([Host]) && afterPeerGrace.Expired.Count == 0, "the host is still held when a guest's grace would be over");
        Check.That(grace.HeldIds.SequenceEqual([Host]), "HeldIds lists the held members");
        Check.That(!grace.ExpiryDue(MemberGrace.PeerGraceMs, HostFirst) && grace.ExpiryDue(MemberGrace.HostGraceMs, HostFirst), "the expiry is due after the host's grace");
        var back = grace.Update(Both, Present(Host, Guest), MemberGrace.HostGraceMs - 1, HostFirst);
        Check.That(back.Returned.SequenceEqual([Host]) && grace.HeldIds.Count == 0, "a host back inside its grace is returned");

        var gone = new MemberGrace();
        gone.Update(Both, Present(Guest), 0, HostFirst);
        var over = gone.Update(Both, Present(Guest), MemberGrace.HostGraceMs, HostFirst);
        Check.That(over.Expired.SequenceEqual([Host]), "the host expires after the host grace");
        Check.That(MemberGrace.HostGraceMs == 180_000, "the host grace is three minutes");

        var mixed = new MemberGrace();
        mixed.Update(Both, Present(), 0, HostFirst);
        var verdict = mixed.Update(Both, Present(), MemberGrace.PeerGraceMs, HostFirst);
        Check.That(verdict.Expired.SequenceEqual([Guest]) && verdict.Held.SequenceEqual([Host]), "with both missing the guest expires first and the host is held");
    }
}
