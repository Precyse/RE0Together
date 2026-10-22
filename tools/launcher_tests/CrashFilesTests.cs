using CoopLauncher;

namespace LauncherTests;

/// <summary>Crash files from a guest to the host: the queue and incident rules, the destination path and names, the transfer into the
/// host's folder, the sender's choice of files and the unpaced flush of the log mirror.</summary>
public static class CrashFilesTests
{
    private const ulong Guest = 76561198000000002;
    private const ulong HostId = 76561198000000001;
    private const string Stamp = "20261022-153000";
    private static readonly DateTime Start = new(2026, 10, 22, 12, 0, 0, DateTimeKind.Utc);

    public static void Run()
    {
        var dir = Path.Combine(Path.GetTempPath(), "cf_crash_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(dir);
        try
        {
            PathsAndNames(dir);
            QueueRules();
            IncidentRules();
            ReceiverWritesTheFilesAndAnnouncesOnce(Path.Combine(dir, "receive"));
            ReceiverRefusesWhatItShouldNotTake(Path.Combine(dir, "refuse"));
            SenderQueuesNewFilesThenTheLog(Path.Combine(dir, "send"));
            FlushLiftsThePaceOfTheLogMirror(Path.Combine(dir, "mirror"));
        }
        finally
        {
            Directory.Delete(dir, recursive: true);
        }
    }

    private static void PathsAndNames(string dir)
    {
        Check.That(CrashRules.Destination(dir, Guest, Stamp) == Path.Combine(dir, "coop", "peer_crash", $"{Guest}_{Stamp}"), "the host folder is coop\\peer_crash\\<steamid>_<stamp>");
        Check.That(CrashRules.TrySplitSpec("coop/dumps/*.dmp", out var folder, out var pattern) && folder == "coop/dumps" && pattern == "*.dmp", "a spec splits into folder and pattern");
        Check.That(CrashRules.TrySplitSpec("Juiced\\CrashDumps\\*.log", out folder, out pattern) && folder == "Juiced\\CrashDumps" && pattern == "*.log", "a backslash spec splits the same way");
        Check.That(CrashRules.TrySplitSpec("a.log", out folder, out pattern) && folder == "" && pattern == "a.log", "a spec without a folder is the game folder");
        Check.That(!CrashRules.TrySplitSpec("coop/dumps/", out _, out _), "a spec without a pattern is refused");
        Check.That(CrashRules.FlatName("coop\\dumps/a b.dmp") == "coop_dumps_a b.dmp", "a path becomes one file name");
        var wire = CrashRules.WireName(Stamp, "coop_a.log");
        Check.That(CrashRules.TryParseWireName(wire, out var stamp, out var file) && stamp == Stamp && file == "coop_a.log", "a wire name round-trips");
        Check.That(!CrashRules.TryParseWireName("coop_a.log", out _, out _), "a name without a stamp is refused");
        Check.That(!CrashRules.TryParseWireName("..\\x/a.log", out _, out _), "a stamp that is not a stamp is refused");
        Check.That(!CrashRules.TryParseWireName(Stamp + "/../a.log", out _, out _), "a file part with a folder is refused");
        Check.That(!CrashRules.TryParseWireName(Stamp + "/", out _, out _), "an empty file part is refused");
        foreach (var id in GameProfile.ListIds())
            Check.That(GameProfile.Load(id).CrashFiles?.All(spec => CrashRules.TrySplitSpec(spec, out _, out _)) == true, $"{id}: every crash file entry names a pattern");
    }

    private static CrashFileInfo Info(string path, long length, DateTime? modified = null) => new(path, modified ?? Start.AddMinutes(5), length);

    private static void QueueRules()
    {
        var queue = new CrashQueue(Start);
        Check.That(queue.Take(0, [Info("a.dmp", 10, Start.AddSeconds(-1))]).Ready.Count == 0
                   && queue.Take(CrashQueue.StableMs * 2, [Info("a.dmp", 10, Start.AddSeconds(-1))]).Ready.Count == 0, "a file older than the session start is never sent");

        queue = new CrashQueue(Start);
        Check.That(queue.Take(0, [Info("a.dmp", 10)]).Ready.Count == 0, "a new file waits for its size to hold");
        Check.That(queue.Take(CrashQueue.StableMs - 1, [Info("a.dmp", 10)]).Ready.Count == 0, "unchanged for less than the stable time: not yet");
        Check.That(queue.Take(CrashQueue.StableMs, [Info("a.dmp", 10)]).Ready.SequenceEqual(["a.dmp"]), "unchanged for the stable time: ready");
        Check.That(queue.Take(CrashQueue.StableMs * 5, [Info("a.dmp", 10)]).Ready.Count == 0, "a file is handed over once");
        Check.That(queue.Take(CrashQueue.StableMs * 6, [Info("a.dmp", 10, Start.AddMinutes(9))]).Ready.Count == 0
                   && queue.Take(CrashQueue.StableMs * 8, [Info("a.dmp", 10, Start.AddMinutes(9))]).Ready.Count == 1, "the same path written again is a new version");

        queue = new CrashQueue(Start);
        queue.Take(0, [Info("b.dmp", 10)]);
        Check.That(queue.Take(CrashQueue.StableMs, [Info("b.dmp", 20)]).Ready.Count == 0, "a file that grew starts waiting again");
        Check.That(queue.Take(CrashQueue.StableMs * 2, [Info("b.dmp", 20)]).Ready.Count == 1, "and is ready once it holds");

        queue = new CrashQueue(Start);
        queue.Take(0, [Info("e.dmp", 0)]);
        Check.That(queue.Take(CrashQueue.StableMs * 3, [Info("e.dmp", 0)]).Ready.Count == 0, "an empty file is not sent");

        queue = new CrashQueue(Start);
        queue.Take(0, [Info("big.dmp", CrashRules.MaxFileBytes + 1), Info("max.dmp", CrashRules.MaxFileBytes)]);
        var scan = queue.Take(CrashQueue.StableMs, [Info("big.dmp", CrashRules.MaxFileBytes + 1), Info("max.dmp", CrashRules.MaxFileBytes)]);
        Check.That(scan.TooLarge.SequenceEqual(["big.dmp"]) && scan.Ready.SequenceEqual(["max.dmp"]), "over 64 MB is dropped, exactly 64 MB is sent");
    }

    private static CrashIncidents Incidents()
    {
        var count = 0;
        return new CrashIncidents(() => $"20261022-1530{++count:D2}");
    }

    private static void IncidentRules()
    {
        var incidents = Incidents();
        Check.That(incidents.Next(0, false, GameProcess.Gone) == null, "a game that never ran opens nothing");
        Check.That(incidents.Next(1000, false, GameProcess.Running) == null, "a running game opens nothing");
        var byFile = incidents.Next(2000, true, GameProcess.Running);
        Check.That(byFile is { Opened: true, SendLog: false }, "a crash file opens an incident, the log waits for the game to be gone");
        Check.That(incidents.Next(3000, false, GameProcess.Running) is { Opened: false, SendLog: false }, "inside the window the incident goes on");
        var gone = incidents.Next(4000, false, GameProcess.Gone);
        Check.That(gone is { Opened: false, SendLog: true } && gone.Value.Stamp == byFile!.Value.Stamp, "the game exits: the log joins the same incident");
        Check.That(incidents.Next(5000, false, GameProcess.Gone) is { SendLog: false }, "the log goes once");

        incidents = Incidents();
        incidents.Next(0, false, GameProcess.Running);
        Check.That(incidents.Next(1000, false, GameProcess.Gone) is { Opened: true, SendLog: true }, "the game exits with no file: an incident with the log");

        incidents = Incidents();
        incidents.Next(0, false, GameProcess.NotResponding);
        Check.That(incidents.Next(CrashIncidents.StalledAfterMs - 1, false, GameProcess.NotResponding) == null, "silent for less than the stall time: nothing");
        Check.That(incidents.Next(CrashIncidents.StalledAfterMs, false, GameProcess.NotResponding) is { Opened: true, SendLog: true }, "silent for the stall time: an incident with the log");
        incidents.Next(CrashIncidents.StalledAfterMs + 1, false, GameProcess.Running);
        Check.That(incidents.Next(CrashIncidents.StalledAfterMs + 2, false, GameProcess.NotResponding) is { Opened: false }, "a window that answered in between starts the wait over");

        incidents = Incidents();
        incidents.Next(0, true, GameProcess.Running);
        var later = incidents.Next(CrashIncidents.WindowMs + 1, true, GameProcess.Running);
        Check.That(later is { Opened: true } && later.Value.Stamp != "20261022-153001", "after the window the next crash is a new incident");

        incidents = Incidents();
        for (var i = 0; i < CrashRules.MaxIncidents; ++i)
            Check.That(incidents.Next((i + 1) * (CrashIncidents.WindowMs + 1), true, GameProcess.Running) is { Opened: true }, $"incident {i + 1} of {CrashRules.MaxIncidents} opens");
        Check.That(incidents.Next(10 * (CrashIncidents.WindowMs + 1), true, GameProcess.Running) == null && incidents.CapReached, "the fourth incident of a session is refused");
    }

    private sealed record Link(RecordingTransport Guest, RecordingTransport Host, FileStreamer Streamer, CrashReceiver Receiver);

    /// <summary>Carries the frames both ways until nothing is left to deliver.</summary>
    private static void Deliver(Link link)
    {
        var guestSeen = 0;
        var hostSeen = 0;
        for (var round = 0; round < 200; ++round)
        {
            link.Streamer.Pump();
            foreach (var (_, frame) in link.Guest.Sent.Skip(guestSeen).ToList()) link.Receiver.OnFrame(Guest, frame);
            guestSeen = link.Guest.Sent.Count;
            foreach (var (_, frame) in link.Host.Sent.Skip(hostSeen).ToList()) link.Streamer.OnAck(HostId, frame);
            hostSeen = link.Host.Sent.Count;
        }
    }

    private static Link NewLink(string gameDir, Func<ulong, string>? nameOf = null)
    {
        var guestTransport = new RecordingTransport();
        var hostTransport = new RecordingTransport();
        var clock = 0L;
        return new Link(guestTransport, hostTransport, new FileStreamer(guestTransport, "Crash files", () => clock += 1000),
            new CrashReceiver(gameDir, hostTransport, nameOf ?? (_ => "Bob")));
    }

    private static void ReceiverWritesTheFilesAndAnnouncesOnce(string gameDir)
    {
        var link = NewLink(gameDir);
        var dump = new byte[5000];
        new Random(7).NextBytes(dump);
        link.Streamer.Enqueue(HostId, CrashRules.WireName(Stamp, "coop_dumps_a.dmp"), dump);
        link.Streamer.Enqueue(HostId, CrashRules.WireName(Stamp, "coop_a.log"), "log"u8.ToArray());
        var lines = LogCapture.During(() => Deliver(link));
        var folder = CrashRules.Destination(gameDir, Guest, Stamp);
        Check.That(File.Exists(Path.Combine(folder, "coop_dumps_a.dmp")) && File.ReadAllBytes(Path.Combine(folder, "coop_dumps_a.dmp")).SequenceEqual(dump), "the dump arrives whole in the incident folder");
        Check.That(File.Exists(Path.Combine(folder, "coop_a.log")), "the log arrives in the same folder");
        Check.That(!Directory.GetFiles(folder, "*.part").Any(), "no temp file is left");
        Check.That(lines.Count(line => line.Contains("Crash files from Bob saved")) == 1, "one host log line for the folder");
        Check.That(link.Host.Of(Msg.FileAck).All(ack => FileMessages.TryParseAck(ack.Payload, out _, out var ok) && ok), "every file is ACKed ok");
    }

    private static void ReceiverRefusesWhatItShouldNotTake(string gameDir)
    {
        var link = NewLink(gameDir);
        link.Streamer.Enqueue(HostId, "no_stamp.dmp", [1]);
        link.Streamer.Enqueue(HostId, Stamp + "/..\\x.dmp", [1]);
        for (var i = 0; i < CrashRules.MaxIncidents + 1; ++i)
            link.Streamer.Enqueue(HostId, CrashRules.WireName($"20261022-15300{i}", "f.dmp"), [1]);
        LogCapture.During(() => Deliver(link));
        var folders = Directory.Exists(Path.Combine(gameDir, "coop", CrashRules.PeerFolder)) ? Directory.GetDirectories(Path.Combine(gameDir, "coop", CrashRules.PeerFolder)) : [];
        Check.That(folders.Length == CrashRules.MaxIncidents, "a guest gets three incident folders and no more");
        Check.That(folders.All(folder => File.Exists(Path.Combine(folder, "f.dmp"))), "each folder holds its file");
        Check.That(!Directory.GetFiles(gameDir, "*.dmp", SearchOption.AllDirectories).Any(path => Path.GetFileName(path) != "f.dmp"), "a bad name writes nothing");
    }

    private static void SenderQueuesNewFilesThenTheLog(string gameDir)
    {
        var dumps = Path.Combine(gameDir, "coop", "dumps");
        Directory.CreateDirectory(dumps);
        File.WriteAllText(Path.Combine(gameDir, "coop", "a.log"), "adapter log");
        File.WriteAllText(Path.Combine(dumps, "old.dmp"), "old");
        File.SetLastWriteTimeUtc(Path.Combine(dumps, "old.dmp"), Start.AddHours(-2));
        File.WriteAllText(Path.Combine(dumps, "new.dmp"), "new");
        File.SetLastWriteTimeUtc(Path.Combine(dumps, "new.dmp"), Start.AddMinutes(1));
        var profile = new GameProfile("t", "Test", 1, "t.exe", 2, 1, [], "", AdapterLog: "coop/a.log", CrashFiles: ["coop/dumps/*.dmp"]);
        var transport = new RecordingTransport();
        var sender = new CrashSender(profile, gameDir, () => HostId, transport, null, Start);

        sender.Poll(0, GameProcess.Running);
        sender.Streamer.Pump();
        Check.That(!transport.Of(Msg.FileBegin).Any(), "a file still settling is not sent");
        sender.Poll(CrashQueue.StableMs, GameProcess.Running);
        sender.Streamer.Pump();
        var names = NamesSent(transport);
        Check.That(names.Count == 1 && names[0].EndsWith("/coop_dumps_new.dmp"), "the new stable file is sent, the old one is not");
        sender.Poll(CrashQueue.StableMs + 1000, GameProcess.Gone);
        sender.Streamer.Pump();
        names = NamesSent(transport);
        Check.That(names.Count == 2 && names[1].EndsWith("/coop_a.log") && names[0][..names[0].IndexOf('/')] == names[1][..names[1].IndexOf('/')], "the game is gone: the whole adapter log joins the same folder");
        sender.Poll(CrashQueue.StableMs + 2000, GameProcess.Gone);
        sender.Streamer.Pump();
        Check.That(NamesSent(transport).Count == 2, "nothing is sent twice");
    }

    private static List<string> NamesSent(RecordingTransport transport) =>
        transport.Of(Msg.FileBegin).Select(frame => FileMessages.TryParseBegin(frame.Payload, out var begin) ? begin.Name : "").ToList();

    private static void FlushLiftsThePaceOfTheLogMirror(string gameDir)
    {
        Directory.CreateDirectory(gameDir);
        var logPath = Path.Combine(gameDir, "a.log");
        const int logBytes = 100 * 1024;
        File.WriteAllBytes(logPath, []);
        var transport = new RecordingTransport();
        var forwarder = new LogForwarder(logPath, transport, () => HostId, isHost: false);
        File.WriteAllBytes(logPath, new byte[logBytes]);
        forwarder.Pump();
        var firstPass = transport.Of(Msg.LogAppend).Sum(frame => frame.Payload.Length);
        forwarder.Pump();
        Check.That(firstPass < logBytes && transport.Of(Msg.LogAppend).Sum(frame => frame.Payload.Length) == firstPass, "the mirror sends one chunk per interval");
        forwarder.Flush();
        forwarder.Pump();
        Check.That(transport.Of(Msg.LogAppend).Sum(frame => frame.Payload.Length) == logBytes, "a flush sends the rest at once");
        File.AppendAllText(logPath, "more");
        forwarder.Pump();
        Check.That(transport.Of(Msg.LogAppend).Sum(frame => frame.Payload.Length) == logBytes, "once caught up the pace is back");
    }
}
