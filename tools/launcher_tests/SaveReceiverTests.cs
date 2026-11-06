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
        public Rig(bool gameRunning, Action<string>? resignFile = null)
        {
            Root = Path.Combine(Path.GetTempPath(), "cf_receiver_" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(Root);
            Session = Path.Combine(Root, "session");
            Staging = Path.Combine(Session, SaveReceiver.StagingFolder);
            Transport = new RecordingTransport();
            Receiver = new SaveReceiver(new SaveSyncProfile([SaveName], "coop/session"), Session, Staging, Transport, () => Host, gameRunning, resignFile);
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

        public List<bool> Acks() => Transport.Of(Msg.FileAck).Select(frame => FileMessages.TryParseAck(frame.Payload, out _, out var ok) && ok && !FileMessages.IsRejection(frame.Payload)).ToList();

        public void Dispose() => Directory.Delete(Root, recursive: true);
    }

    public static void Run()
    {
        FirstSetGoesToSession();
        RunningGameStagesTheFirstSet();
        LockedTempFileAcksFalse();
        LockedTargetAcksFalse();
        ResignedBeforeItIsMoved();
        UnresignableFileIsRefused();
        ManifestListsTheFilesTheHostHas();
        AnotherWorldStartsTheSetAgain();
        ManifestRemovesFilesTheHostLacks();
    }

    private static void ManifestRemovesFilesTheHostLacks()
    {
        const string StaleName = "old.bin";
        using var closed = new Rig(gameRunning: false);
        Directory.CreateDirectory(Path.Combine(closed.Session, "folder"));
        File.WriteAllText(Path.Combine(closed.Session, StaleName), "x");
        File.WriteAllText(Path.Combine(closed.Session, StaleName + SaveReceiver.TempSuffix), "x");
        File.WriteAllText(Path.Combine(closed.Session, SaveName), "x");
        File.WriteAllText(Path.Combine(closed.Session, SaveName + SaveReceiver.TempSuffix), "x");
        closed.Receiver.OnFrame(Host, FileMessages.Manifest([SaveName]));
        Check.That(!File.Exists(Path.Combine(closed.Session, StaleName)) && !File.Exists(Path.Combine(closed.Session, StaleName + SaveReceiver.TempSuffix)),
            "stale: a file the manifest does not list is removed when it arrives with the game closed");
        Check.That(File.Exists(Path.Combine(closed.Session, SaveName)) && File.Exists(Path.Combine(closed.Session, SaveName + SaveReceiver.TempSuffix))
            && Directory.Exists(Path.Combine(closed.Session, "folder")), "stale: listed files, their half-received copies and folders stay");

        using var running = new Rig(gameRunning: true);
        Directory.CreateDirectory(running.Session);
        File.WriteAllText(Path.Combine(running.Session, StaleName), "x");
        running.Receiver.OnFrame(Host, FileMessages.Manifest([SaveName]));
        Check.That(File.Exists(Path.Combine(running.Session, StaleName)), "stale: a running game keeps its files when the manifest arrives");
        running.Receiver.PromoteStaged();
        Check.That(!File.Exists(Path.Combine(running.Session, StaleName)), "stale: they are dropped at the next staging promotion");
    }

    private static void ManifestListsTheFilesTheHostHas()
    {
        using var rig = new Rig(gameRunning: false);
        rig.Receiver.OnFrame(Host, FileMessages.Manifest([]));
        Check.That(rig.Receiver.Complete, "a fixed list: the host's manifest of fewer files is the set to wait for");
    }

    private static void AnotherWorldStartsTheSetAgain()
    {
        using var rig = new Rig(gameRunning: false);
        rig.Receiver.OnFrame(Host, FileMessages.Manifest([SaveName]));
        rig.Deliver(1);
        Check.That(rig.Receiver.Complete && File.Exists(Path.Combine(rig.Session, SaveName)), "the first world arrives");
        Directory.CreateDirectory(rig.Staging);
        File.WriteAllText(Path.Combine(rig.Staging, "old.bin"), "x");
        rig.Receiver.OnFrame(Host, FileMessages.Manifest([SaveName], generation: 1));
        Check.That(!rig.Receiver.Complete && !File.Exists(Path.Combine(rig.Session, SaveName)) && !Directory.Exists(rig.Staging), "a new generation drops the old set and the staging folder");
        rig.Deliver(2);
        Check.That(rig.Receiver.Complete && File.Exists(Path.Combine(rig.Session, SaveName)), "the new world is written to the session folder again");
        rig.Receiver.OnFrame(Host, FileMessages.Manifest([SaveName], generation: 1));
        Check.That(rig.Receiver.Complete, "the same generation again changes nothing");
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

    private static void ResignedBeforeItIsMoved()
    {
        byte[] resigned = [9, 9, 9];
        using var rig = new Rig(gameRunning: false, path => File.WriteAllBytes(path, resigned));
        rig.Deliver(1);
        Check.That(rig.Receiver.Complete && File.ReadAllBytes(Path.Combine(rig.Session, SaveName)).SequenceEqual(resigned),
            "a re-signing profile: the session holds the re-signed bytes");
        Check.That(rig.Acks().SequenceEqual([true]), "a re-signing profile: the re-signed file is acknowledged");
    }

    private static void UnresignableFileIsRefused()
    {
        using var rig = new Rig(gameRunning: false, _ => throw new InvalidDataException("not this account"));
        rig.Deliver(1);
        Check.That(!rig.Receiver.Complete && !File.Exists(Path.Combine(rig.Session, SaveName)), "a file that cannot be re-signed never reaches the session");
        Check.That(rig.Acks().SequenceEqual([false]) && !File.Exists(Path.Combine(rig.Session, SaveName + SaveReceiver.TempSuffix)),
            "a file that cannot be re-signed is refused with a negative ACK and no temp file is left");
        Check.That(rig.Transport.Of(Msg.FileAck).All(frame => FileMessages.IsRejection(frame.Payload)) && rig.Receiver.Failure is { File: SaveName, Reason: "not this account" },
            "a file that cannot be re-signed tells the host not to send it again and is named as the failure");
    }
}
