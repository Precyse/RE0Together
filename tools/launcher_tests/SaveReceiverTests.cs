using System.Security.Cryptography;
using CoopLauncher;

namespace LauncherTests;

/// <summary>SaveReceiver on temp folders: the first set goes to staging while the game runs; a locked file fails one transfer, never the caller.</summary>
public static class SaveReceiverTests
{
    private const ulong Host = 7;
    private const string SaveName = "data0.bin";
    private static readonly byte[] Content = [1, 2, 3, 4, 5, 6, 7, 8];

    private sealed class Rig : IDisposable
    {
        public Rig(bool gameRunning)
        {
            Root = Path.Combine(Path.GetTempPath(), "cf_receiver_" + Guid.NewGuid().ToString("N"));
            Session = Path.Combine(Root, "session");
            Staging = Path.Combine(Session, SaveReceiver.StagingFolder);
            Transport = new RecordingTransport();
            Receiver = new SaveReceiver(new SaveSyncProfile([SaveName], "coop/session"), Session, Staging, Transport, () => Host, gameRunning);
        }

        public string Root { get; }
        public string Session { get; }
        public string Staging { get; }
        public RecordingTransport Transport { get; }
        public SaveReceiver Receiver { get; }

        public void Deliver(uint id)
        {
            Receiver.OnFrame(Host, FileMessages.Begin(new FileBegin(id, (uint)Content.Length, SHA256.HashData(Content), SaveName)));
            Receiver.OnFrame(Host, FileMessages.Chunk(id, 0, Content));
            Receiver.OnFrame(Host, FileMessages.End(id));
        }

        public List<bool> Acks() => Transport.Of(Msg.FileAck).Select(frame => FileMessages.TryParseAck(frame.Payload, out _, out var ok) && ok).ToList();

        public void Dispose() => Directory.Delete(Root, recursive: true);
    }

    public static void Run()
    {
        FirstSetGoesToSession();
        RunningGameStagesTheFirstSet();
        LockedTempFileAcksFalse();
        LockedTargetAcksFalse();
    }

    private static void FirstSetGoesToSession()
    {
        using var rig = new Rig(gameRunning: false);
        rig.Deliver(1);
        Check.That(rig.Receiver.Complete && File.Exists(Path.Combine(rig.Session, SaveName)), "the first set lands in the session folder");
        Check.That(rig.Acks().SequenceEqual([true]), "the file is acknowledged");
    }

    private static void RunningGameStagesTheFirstSet()
    {
        using var rig = new Rig(gameRunning: true);
        rig.Deliver(1);
        Check.That(rig.Receiver.Complete, "a running game still completes the first set (the launch gate opens)");
        Check.That(File.Exists(Path.Combine(rig.Staging, SaveName)) && !File.Exists(Path.Combine(rig.Session, SaveName)),
            "a running game: the first set goes to staging, not under the game");
        rig.Receiver.PromoteStaged();
        Check.That(File.ReadAllBytes(Path.Combine(rig.Session, SaveName)).SequenceEqual(Content) && !File.Exists(Path.Combine(rig.Staging, SaveName)),
            "the staged set moves into the session once the game is gone");
    }

    private static void LockedTempFileAcksFalse()
    {
        using var rig = new Rig(gameRunning: false);
        Directory.CreateDirectory(rig.Session);
        using (new FileStream(Path.Combine(rig.Session, SaveName + SaveReceiver.TempSuffix), FileMode.Create, FileAccess.Write, FileShare.None))
            rig.Deliver(1);
        Check.That(!rig.Receiver.Complete, "a locked temp file: the set is not complete");
        Check.That(rig.Acks().SequenceEqual([false]), "a locked temp file: one negative ACK, no exception");
    }

    private static void LockedTargetAcksFalse()
    {
        using var rig = new Rig(gameRunning: false);
        Directory.CreateDirectory(rig.Session);
        var target = Path.Combine(rig.Session, SaveName);
        File.WriteAllText(target, "the guest own save");
        using (new FileStream(target, FileMode.Open, FileAccess.Read, FileShare.None))
            rig.Deliver(1);
        Check.That(!rig.Receiver.Complete && rig.Acks().SequenceEqual([false]), "a locked target: negative ACK, not complete, no exception");
        Check.That(!File.Exists(target + SaveReceiver.TempSuffix), "the temp file of the failed transfer is removed");

        rig.Deliver(2);
        Check.That(rig.Receiver.Complete && File.ReadAllBytes(target).SequenceEqual(Content), "the resend after the lock is gone completes the set");
    }
}
