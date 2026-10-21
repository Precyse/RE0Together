using CoopLauncher;

namespace LauncherTests;

/// <summary>LogForwarder peer log naming and retention: one file per peer and session, the newest ten kept per peer, dumps untouched.</summary>
public static class LogForwarderTests
{
    private const ulong Peer = 76561198000000001;

    public static void Run()
    {
        var names = Enumerable.Range(0, LogForwarder.KeptPeerLogs + 3).Select(i => $"peer_{Peer}_202601{i + 1:D2}-120000.log").ToList();
        var expired = LogForwarder.ExpiredPeerLogs(names.OrderBy(_ => Guid.NewGuid()));
        Check.That(expired.OrderBy(n => n, StringComparer.Ordinal).SequenceEqual(names.Take(3)), "the oldest logs beyond the newest ten are expired, whatever the listing order");
        Check.That(LogForwarder.ExpiredPeerLogs(names.Take(LogForwarder.KeptPeerLogs)).Count == 0, "ten logs or fewer: none expire");

        var withLegacy = names.Take(LogForwarder.KeptPeerLogs).Append($"peer_{Peer}.log").ToList();
        Check.That(LogForwarder.ExpiredPeerLogs(withLegacy).SequenceEqual([$"peer_{Peer}.log"]), "the older unstamped log expires first");

        Check.That(LogForwarder.IsPeerLog($"peer_{Peer}_20260101-120000.log", Peer), "a stamped log belongs to its peer");
        Check.That(LogForwarder.IsPeerLog($"peer_{Peer}.log", Peer), "the older unstamped log belongs to its peer");
        Check.That(!LogForwarder.IsPeerLog($"peer_{Peer}_crash-1.dmp", Peer), "a crash dump is not a peer log");
        Check.That(!LogForwarder.IsPeerLog("peer_1234_20260101-120000.log", 123), "another peer whose id starts the same is not this peer");
        Check.That(!LogForwarder.IsPeerLog("adapter.log", Peer), "the host adapter log is not a peer log");
    }
}
