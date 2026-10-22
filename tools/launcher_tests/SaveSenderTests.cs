using CoopLauncher;

namespace LauncherTests;

/// <summary>An ITransport that records what is sent and accepts everything.</summary>
internal sealed class RecordingTransport : ITransport
{
    public List<(ulong Peer, Frame Frame)> Sent { get; } = [];

    public ulong LocalId => 1;

    public event Action<ulong, Frame> FrameReceived
    {
        add { }
        remove { }
    }

    public int MaxChunkBytes => 1024;

    public bool Send(ulong peerId, Frame frame, TransportChannel channel = TransportChannel.Main)
    {
        Sent.Add((peerId, frame));
        return true;
    }

    public void Pump()
    {
    }

    public void Dispose()
    {
    }

    public IEnumerable<Frame> Of(ushort type) => Sent.Select(sent => sent.Frame).Where(frame => frame.Type == type);
}

/// <summary>SaveSender on temp folders: a missing save folder is an empty set; a set the guest could not take is not sent.</summary>
public static class SaveSenderTests
{
    private const ulong Peer = 7;
    private const int OverManifestLimit = FileMessages.MaxManifestNames + 1;

    private static readonly SaveSyncProfile Pattern = new([], "coop/session", FilePattern: "*.dat");

    public static void Run()
    {
        var dir = Path.Combine(Path.GetTempPath(), "cf_sender_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(dir);
        try
        {
            MissingFolderIsAnEmptySet(Path.Combine(dir, "missing"));
            NormalSetAnnouncesItsNames(Path.Combine(dir, "normal"));
            TooManyNamesAreNotSent(Path.Combine(dir, "many"));
            NonAsciiNameIsNotSent(Path.Combine(dir, "unicode"));
            OversizedFileIsNotSent(Path.Combine(dir, "big"));
            FixedListWithMissingFolderDoesNotThrow(Path.Combine(dir, "fixed"));
            RejectedFileWaitsBeforeItIsResent(Path.Combine(dir, "rejected"));
        }
        finally
        {
            Directory.Delete(dir, recursive: true);
        }
    }

    private static void MissingFolderIsAnEmptySet(string folder)
    {
        var transport = new RecordingTransport();
        var sender = new SaveSender(Pattern, folder, transport);
        sender.SendTo(Peer);
        sender.SendTo(Peer);
        var manifests = transport.Of(Msg.FileManifest).ToList();
        Check.That(manifests.Count == 2 && manifests.All(m => FileMessages.TryParseManifest(m.Payload, out var names) && names.Count == 0),
            "a missing save folder sends an empty manifest (the guest completes)");
        sender.Pump();
        Check.That(!transport.Of(Msg.FileBegin).Any(), "a missing save folder sends no file");
    }

    private static void NormalSetAnnouncesItsNames(string folder)
    {
        Directory.CreateDirectory(folder);
        File.WriteAllText(Path.Combine(folder, "b.dat"), "b");
        File.WriteAllText(Path.Combine(folder, "a.dat"), "a");
        File.WriteAllText(Path.Combine(folder, "skip.txt"), "x");
        var transport = new RecordingTransport();
        var sender = new SaveSender(Pattern, folder, transport);
        sender.SendTo(Peer);
        sender.Pump();
        var manifest = transport.Of(Msg.FileManifest).Single();
        Check.That(FileMessages.TryParseManifest(manifest.Payload, out var names) && names.SequenceEqual(["a.dat", "b.dat"]), "the manifest lists the matching files in order");
        Check.That(transport.Of(Msg.FileBegin).Count() == 2, "each matching file starts a transfer");
    }

    private static void TooManyNamesAreNotSent(string folder)
    {
        Directory.CreateDirectory(folder);
        for (var i = 0; i < OverManifestLimit; i++) File.WriteAllText(Path.Combine(folder, $"s{i:D3}.dat"), "x");
        var transport = new RecordingTransport();
        var sender = new SaveSender(Pattern, folder, transport);
        sender.SendTo(Peer);
        sender.Pump();
        Check.That(transport.Sent.Count == 0, "more names than the manifest holds: nothing is sent");
    }

    private static void NonAsciiNameIsNotSent(string folder)
    {
        Directory.CreateDirectory(folder);
        File.WriteAllText(Path.Combine(folder, "später.dat"), "x");
        File.WriteAllText(Path.Combine(folder, "plain.dat"), "x");
        var transport = new RecordingTransport();
        var sender = new SaveSender(Pattern, folder, transport);
        sender.SendTo(Peer);
        sender.Pump();
        Check.That(transport.Sent.Count == 0, "a name the manifest cannot carry: nothing is sent");
    }

    private static void OversizedFileIsNotSent(string folder)
    {
        Directory.CreateDirectory(folder);
        using (var big = File.Create(Path.Combine(folder, "big.dat"))) big.SetLength(SaveReceiver.MaxFileBytes + 1);
        File.WriteAllText(Path.Combine(folder, "small.dat"), "x");
        var transport = new RecordingTransport();
        var sender = new SaveSender(Pattern, folder, transport);
        sender.SendTo(Peer);
        sender.Pump();
        Check.That(transport.Sent.Count == 0, "a file over the guest limit: nothing is sent");
        Check.That(sender.SendChanged(Peer, "big.dat") && transport.Sent.Count == 0, "a changed file over the limit is not sent and not retried");
    }

    private static void FixedListWithMissingFolderDoesNotThrow(string folder)
    {
        var transport = new RecordingTransport();
        var sender = new SaveSender(new SaveSyncProfile(["data0.bin"], "coop/session"), folder, transport);
        sender.SendTo(Peer);
        sender.Pump();
        Check.That(transport.Sent.Count == 0, "a fixed list whose file is missing sends nothing and does not throw");
    }

    private static void RejectedFileWaitsBeforeItIsResent(string folder)
    {
        Directory.CreateDirectory(folder);
        File.WriteAllText(Path.Combine(folder, "a.dat"), "a");
        var transport = new RecordingTransport();
        var now = 0L;
        var sender = new SaveSender(Pattern, folder, transport, () => now);
        sender.SendTo(Peer);
        sender.Pump();
        Check.That(FileMessages.TryParseBegin(transport.Of(Msg.FileBegin).Single().Payload, out var begin), "the file starts a transfer");
        now += FileStreamer.RetryAfterMs / 2;
        sender.OnAck(Peer, FileMessages.Ack(begin.Id, ok: false));
        sender.Pump();
        Check.That(transport.Of(Msg.FileBegin).Count() == 1, "a rejected file is not resent at once (a short lock would burn every attempt)");
        now += FileStreamer.RetryAfterMs;
        sender.Pump();
        sender.Pump();
        Check.That(transport.Of(Msg.FileBegin).Count() == 2, "a rejected file is resent after the retry delay");
    }
}
