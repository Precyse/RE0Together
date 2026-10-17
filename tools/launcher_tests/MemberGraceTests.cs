using CoopLauncher;

namespace LauncherTests;

/// <summary>MemberGrace: a hiccup under 10 s resyncs the member (returned), over 10 s it is gone (expired).</summary>
public static class MemberGraceTests
{
    private const ulong Host = 1;
    private const ulong Guest = 2;
    private static readonly ulong[] Both = [Host, Guest];

    private static HashSet<ulong> Present(params ulong[] ids) => ids.ToHashSet();

    public static void Run()
    {
        Hiccup();
        GoneAfterGrace();
        ExpiryDue();
        HostAndTracking();
    }

    private static void Hiccup()
    {
        var grace = new MemberGrace();
        var all = grace.Update(Both, Present(Host, Guest), 0);
        Check.That(all.Held.Count + all.Returned.Count + all.Expired.Count == 0, "everyone present: nothing to do");

        var gone = grace.Update(Both, Present(Host), 1000);
        Check.That(gone.Held.SequenceEqual([Guest]) && gone.Expired.Count == 0 && gone.Returned.Count == 0, "missing: held");
        var still = grace.Update(Both, Present(Host), 5000);
        Check.That(still.Held.SequenceEqual([Guest]) && still.Expired.Count == 0, "still missing inside the grace: still held");

        var back = grace.Update(Both, Present(Host, Guest), 9000);
        Check.That(back.Returned.SequenceEqual([Guest]) && back.Expired.Count == 0 && back.Held.Count == 0,
            "back inside the grace: returned (resync, not reset)");
        var later = grace.Update(Both, Present(Host, Guest), 9100);
        Check.That(later.Returned.Count == 0, "returned only once");
    }

    private static void GoneAfterGrace()
    {
        var grace = new MemberGrace();
        grace.Update(Both, Present(Host), 0);
        var almost = grace.Update(Both, Present(Host), MemberGrace.PeerGraceMs - 1);
        Check.That(almost.Held.SequenceEqual([Guest]) && almost.Expired.Count == 0, "1 ms before the grace is over: held");
        var over = grace.Update(Both, Present(Host), MemberGrace.PeerGraceMs);
        Check.That(over.Expired.SequenceEqual([Guest]) && over.Held.Count == 0, "grace over: expired (gone)");
        var after = grace.Update([Host], Present(Host), MemberGrace.PeerGraceMs + 1);
        Check.That(after.Expired.Count == 0 && after.Held.Count == 0, "an expired member is forgotten once the slot is dropped");
        var rejoin = grace.Update([Host], Present(Host, Guest), MemberGrace.PeerGraceMs + 2);
        Check.That(rejoin.Returned.Count == 0, "a member back after expiry is not a return (it is a new join)");
    }

    private static void ExpiryDue()
    {
        var grace = new MemberGrace();
        Check.That(!grace.ExpiryDue(1_000_000), "nobody held: nothing due");
        grace.Update(Both, Present(Host), 100);
        Check.That(!grace.ExpiryDue(100 + MemberGrace.PeerGraceMs - 1), "not due inside the grace");
        Check.That(grace.ExpiryDue(100 + MemberGrace.PeerGraceMs), "due when the grace is over");
        grace.Update(Both, Present(Host), 100 + MemberGrace.PeerGraceMs);
        Check.That(!grace.ExpiryDue(100 + MemberGrace.PeerGraceMs), "not due again once expired");
    }

    private static void HostAndTracking()
    {
        var grace = new MemberGrace();
        grace.Update(Both, Present(Guest), 0);
        var over = grace.Update(Both, Present(Guest), MemberGrace.PeerGraceMs);
        Check.That(over.Expired.SequenceEqual([Host]), "the host is held and expires like any member");

        var tracked = new MemberGrace();
        tracked.Update(Both, Present(Host), 0);
        tracked.Update([Host], Present(Host), 50);
        Check.That(!tracked.ExpiryDue(MemberGrace.PeerGraceMs + 100), "a member whose slot was dropped is no longer tracked");

        var cleared = new MemberGrace();
        cleared.Update(Both, Present(Host), 0);
        cleared.Clear();
        Check.That(!cleared.ExpiryDue(MemberGrace.PeerGraceMs + 1), "Clear forgets every hold");
    }
}
