using System.Collections.Concurrent;
using System.Net;
using System.Net.Sockets;
using CoopLauncher;

const ulong HostId = 10;
const ulong GuestA = 20;
const ulong GuestB = 30;
const ushort GameDatagram = 0x0120;
const int BridgePort = 27991;
const int GuestBridgePort = 27992;
const int WaitMs = 3000;
const int GraceWaitMs = (int)MemberGrace.PeerGraceMs + WaitMs;

var failures = 0;

void Check(bool ok, string what)
{
    Console.WriteLine($"{(ok ? "ok  " : "FAIL")} {what}");
    if (!ok) failures++;
}

var profile = GameProfile.Load("sr2");
var lobby = new ScriptedLobby(HostId, [new(HostId, "Host"), new(GuestA, "Alice"), new(GuestB, string.Empty)]);
var transport = new RecordingTransport(HostId);
using var bridge = new LoopbackBridge(profile, BridgePort);
using var session = new Session(profile, lobby, transport, bridge);
using var adapter = new StandInAdapter(BridgePort, profile.Id);

Pump(() => bridge.IsReady && adapter.Count(Msg.PeerUp) >= 2);
Check(adapter.Count(Msg.Welcome) == 1, "the adapter gets WELCOME");
Check(adapter.Slots(Msg.PeerUp).Order().SequenceEqual([(byte)1, (byte)2]), "PEER_UP for slots 1 and 2 (a member with an empty name too)");

adapter.Send(Datagram(slot: 1));
adapter.Send(Datagram(slot: Framing.SlotAll));
Pump(() => transport.SentTo(GuestA) == 2 && transport.SentTo(GuestB) == 1);
Check(transport.SentTo(GuestA) == 2 && transport.SentTo(GuestB) == 1, "unicast to slot 1 and a broadcast reach the right members");

lobby.SetMembers([new(HostId, "Host"), new(GuestB, string.Empty)]);
Pump(() => adapter.Count(Msg.PeerUp) >= 3, GraceWaitMs);
Check(adapter.Slots(Msg.PeerDown).Contains(1) && adapter.Slots(Msg.PeerDown).Contains(2), "PEER_DOWN for the member that left and the one that moved");
Check(adapter.Slots(Msg.PeerUp).Last() == 1, "PEER_UP for the member that moved to slot 1");
var toGuestA = transport.SentTo(GuestA);
var toGuestB = transport.SentTo(GuestB);
adapter.Send(Datagram(slot: 2));
adapter.Send(Datagram(slot: 1));
Pump(() => transport.SentTo(GuestB) == toGuestB + 1);
Check(transport.SentTo(GuestB) == toGuestB + 1 && transport.SentTo(GuestA) == toGuestA,
    "after the re-slot a datagram for the old slot 2 is dropped, one for slot 1 reaches the moved member, none reaches the one that left");

transport.Receive(GuestA, new Frame(GameDatagram, 0, 0, [1]));
transport.Receive(GuestB, new Frame(GameDatagram, 0, 0, [2]));
Pump(() => adapter.Count(GameDatagram) == 1);
Check(adapter.Count(GameDatagram) == 1 && adapter.Slots(GameDatagram).Single() == 1, "a late frame from the member that left is dropped, the moved member's arrives with slot 1");

lobby.SetMembers([new(HostId, "Host")]);
Pump(() => !session.PeerConnected, GraceWaitMs);
Check(!session.PeerConnected && !session.Ended, "the last guest leaving keeps the session open");

lobby.SetMembers([new(GuestB, "Bob")], owner: GuestB);
Pump(() => session.Ended, WaitMs);
Check(!session.Ended, "an owner change alone does not end the session");

GuestSession();

return failures == 0 ? 0 : 1;

void GuestSession()
{
    var guestLobby = new ScriptedLobby(HostId, [new(HostId, "Host"), new(GuestA, "Alice"), new(GuestB, "Bob")]);
    var guestTransport = new RecordingTransport(GuestA);
    using var guestBridge = new LoopbackBridge(profile, GuestBridgePort);
    using var guestSession = new Session(profile, guestLobby, guestTransport, guestBridge);
    using var guestAdapter = new StandInAdapter(GuestBridgePort, profile.Id);
    void PumpGuest(Func<bool> done, int timeoutMs = WaitMs) => PumpWith(guestBridge, guestSession, done, timeoutMs);

    PumpGuest(() => guestBridge.IsReady && guestAdapter.Count(Msg.PeerUp) >= 2);
    Check(guestAdapter.Slots(Msg.PeerUp).Order().SequenceEqual([(byte)0, (byte)2]), "a guest's adapter hears the host (slot 0) and the other guest (slot 2)");
    Check(!guestSession.HostMissing, "the host is in the lobby: not missing");

    var newLobby = new ScriptedLobby(HostId, [new(HostId, "Host"), new(GuestA, "Alice"), new(GuestB, "Bob")], id: 2);
    guestSession.Hold();
    guestSession.Rebind(newLobby);
    PumpGuest(() => guestAdapter.Count(Msg.PeerUp) >= 4);
    Check(guestSession.LobbyId == 2 && !guestSession.Held && !guestSession.Ended, "a new lobby id with the same owner is accepted");
    Check(guestAdapter.Count(Msg.PeerUp) == 4 && guestAdapter.Slots(Msg.PeerUp).Order().SequenceEqual([(byte)0, (byte)0, (byte)2, (byte)2]),
        "the rebind sends PEER_UP again for every slot, none changed");
    guestAdapter.Send(Datagram(slot: 0));
    PumpGuest(() => guestTransport.SentTo(HostId) == 1);
    Check(guestTransport.SentTo(HostId) == 1 && guestAdapter.Count(Msg.PeerDown) == 0, "after the rebind the slots still route to the same members");

    newLobby.SetMembers([new(GuestA, "Alice")], owner: GuestA);
    PumpGuest(() => guestAdapter.Slots(Msg.PeerDown).Contains(2), GraceWaitMs);
    Check(guestAdapter.Slots(Msg.PeerDown).Contains(2), "a member that is not the host expires after the peer grace");
    Check(guestSession.HostMissing && guestSession.IsHeld(HostId) && !guestSession.Ended && !guestAdapter.Slots(Msg.PeerDown).Contains(0),
        "the missing host is still held when a guest's grace is over: the session goes on");
}

Frame Datagram(byte slot) => new(GameDatagram, 0, slot, [0, 0, 0, 0, 7]);

void Pump(Func<bool> done, int timeoutMs = WaitMs) => PumpWith(bridge, session, done, timeoutMs);

void PumpWith(LoopbackBridge pumpedBridge, Session pumpedSession, Func<bool> done, int timeoutMs)
{
    var deadline = Environment.TickCount64 + timeoutMs;
    while (Environment.TickCount64 < deadline)
    {
        pumpedBridge.Pump();
        pumpedSession.Pump();
        if (done()) return;
        Thread.Sleep(5);
    }
}

sealed class ScriptedLobby(ulong ownerId, List<LobbyMember> members, ulong id = 1) : ILobby
{
    public bool IsReady => true;
    public LobbyFailure? Failure => null;
    public string? GameId => "sr2";
    public ulong Id => id;
    public ulong OwnerId { get; private set; } = ownerId;
    public IReadOnlyDictionary<string, string> HostOptions { get; } = new Dictionary<string, string>();
    public string JoinSecret => string.Empty;
    public IReadOnlyList<LobbyMember> Members { get; private set; } = members;
    public int Revision { get; private set; }

    /// <summary>The members now in the lobby; the owner moves on when the host left (as Steam hands the lobby over).</summary>
    public void SetMembers(List<LobbyMember> next, ulong? owner = null)
    {
        Members = next;
        if (owner is { } newOwner) OwnerId = newOwner;
        Revision++;
    }

    public void SetLobbyData(LobbyKey key, string value) { }
    public LobbyListing? Listing => null;
    public void SetMemberData(MemberKey key, string value) { }
    public string MemberData(LobbyMember member, MemberKey key) => string.Empty;
    public void Pump() { }
    public void Dispose() { }
}

sealed class RecordingTransport(ulong localId) : ITransport
{
    private readonly ConcurrentDictionary<ulong, int> _sent = new();

    public ulong LocalId { get; } = localId;
    public int MaxChunkBytes => 1024;
    public event Action<ulong, Frame>? FrameReceived;

    public bool Send(ulong peerId, Frame frame, TransportChannel channel = TransportChannel.Main)
    {
        if (frame.Type >= Msg.FirstGameType) _sent.AddOrUpdate(peerId, 1, (_, n) => n + 1);  // pings and the like are not counted
        return true;
    }

    public int SentTo(ulong peerId) => _sent.GetValueOrDefault(peerId);
    public void Receive(ulong sender, Frame frame) => FrameReceived?.Invoke(sender, frame);
    public void Pump() { }
    public void Dispose() { }
}

sealed class StandInAdapter : IDisposable
{
    private readonly TcpClient _client = new();
    private readonly ConcurrentQueue<Frame> _frames = new();

    public StandInAdapter(int port, string gameId)
    {
        _client.Connect(IPAddress.Loopback, port);
        var id = System.Text.Encoding.ASCII.GetBytes(gameId);
        var hello = new byte[3 + id.Length];
        BitConverter.GetBytes(Framing.ProtocolVersion).CopyTo(hello, 0);
        hello[2] = (byte)id.Length;
        id.CopyTo(hello, 3);
        Send(new Frame(Msg.Hello, Framing.FlagReliable, 0, hello));
        new Thread(ReadLoop) { IsBackground = true }.Start();
    }

    public void Send(Frame frame)
    {
        lock (_client) _client.GetStream().Write(Framing.EncodeLoopback(frame));
    }

    public int Count(ushort type) => _frames.Count(f => f.Type == type);

    public List<byte> Slots(ushort type) =>
        _frames.Where(f => f.Type == type).Select(f => type is Msg.PeerUp or Msg.PeerDown ? f.Payload[0] : f.Slot).ToList();

    public void Dispose() => _client.Close();

    private void ReadLoop()
    {
        var stream = _client.GetStream();
        while (Framing.TryReadLoopback(stream, out var frame))
        {
            if (frame.Type == Msg.Heartbeat) Send(frame);
            else _frames.Enqueue(frame);
        }
    }
}
