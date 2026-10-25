using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
using System.Text;
using CoopLauncher;

namespace LauncherTests;

/// <summary>LoopbackBridge on an ephemeral 127.0.0.1 port with plain sockets as adapters: a second connection never takes the link
/// before its HELLO, and a game that does not read never blocks the sender.</summary>
public static class LoopbackBridgeTests
{
    private const string GameId = "t";
    private const ushort GameFrameType = Msg.FirstGameType;
    private const int WaitMs = 3000;
    private const int PollMs = 5;
    private const int ShortLinkTimeoutMs = 300;
    private const int FloodFrames = 40_000;
    private const int FloodPayloadBytes = 1024;
    private const int ReliableFrames = 300;
    private const int ReliablePayloadBytes = 64 * 1024;
    private const long NeverBlockedMs = 3000;

    private static readonly GameProfile Profile = new(GameId, "Test", 1, "t.exe", 2, 1, [], "coop/adapter.ini");

    private sealed class Adapter : IDisposable
    {
        private readonly TcpClient _client = new();

        public Adapter(int port)
        {
            _client.Connect(IPAddress.Loopback, port);
            _client.ReceiveTimeout = WaitMs;
        }

        public void Send(Frame frame) => _client.GetStream().Write(Framing.EncodeLoopback(frame));

        public void Hello(string gameId, ushort proto = Framing.ProtocolVersion)
        {
            var id = Encoding.ASCII.GetBytes(gameId);
            var payload = new byte[3 + id.Length];
            BitConverter.TryWriteBytes(payload, proto);
            payload[2] = (byte)id.Length;
            id.CopyTo(payload, 3);
            Send(new Frame(Msg.Hello, Framing.FlagReliable, 0, payload));
        }

        /// <summary>The next frame of the wanted type, or null when the bridge closed the connection or nothing came in time.</summary>
        public Frame? ReadOf(ushort type)
        {
            while (Framing.TryReadLoopback(_client.GetStream(), out var frame))
                if (frame.Type == type) return frame;
            return null;
        }

        /// <summary>True when the bridge closed the connection (everything still unread is skipped); false when it stays open.</summary>
        public bool ClosedByBridge()
        {
            var buffer = new byte[4096];
            try
            {
                while (_client.GetStream().Read(buffer) > 0)
                {
                }
                return true;
            }
            catch (IOException)
            {
                return false;
            }
        }

        public void Dispose() => _client.Dispose();
    }

    public static void Run()
    {
        HelloLinksTheAdapter();
        SecondConnectionKeepsTheLinkUntilItsHello();
        PendingConnectionWithoutHelloIsClosed();
        FloodOfUnreliableFramesNeverBlocksSend();
        BackedUpReliableFramesDropTheLink();
        OptionSetReachesTheEventAndLobbyStateTheAdapter();
    }

    private static void PumpUntil(LoopbackBridge bridge, Func<bool> done)
    {
        var watch = Stopwatch.StartNew();
        while (!done() && watch.ElapsedMilliseconds < WaitMs)
        {
            bridge.Pump();
            Thread.Sleep(PollMs);
        }
    }

    private static void PumpFor(LoopbackBridge bridge, int ms)
    {
        var watch = Stopwatch.StartNew();
        while (watch.ElapsedMilliseconds < ms)
        {
            bridge.Pump();
            Thread.Sleep(PollMs);
        }
    }

    private static void HelloLinksTheAdapter()
    {
        using var bridge = new LoopbackBridge(Profile, 0);
        var ready = 0;
        bridge.AdapterReady += () => ready++;
        using var adapter = new Adapter(bridge.Port);
        PumpFor(bridge, 100);
        Check.That(!bridge.IsReady, "a connection is not a link before its HELLO");
        adapter.Hello(GameId);
        PumpUntil(bridge, () => bridge.IsReady);
        Check.That(bridge.IsReady && ready == 1, "a valid HELLO links the adapter once");
        Check.That(adapter.ReadOf(Msg.Heartbeat) != null, "the linked adapter gets the heartbeat");
    }

    private static void SecondConnectionKeepsTheLinkUntilItsHello()
    {
        using var bridge = new LoopbackBridge(Profile, 0);
        var ready = 0;
        var gameFrames = 0;
        bridge.AdapterReady += () => ready++;
        bridge.GameFrame += _ => gameFrames++;
        using var first = new Adapter(bridge.Port);
        first.Hello(GameId);
        PumpUntil(bridge, () => bridge.IsReady);

        using var scanner = new Adapter(bridge.Port);
        PumpFor(bridge, 200);
        first.Send(new Frame(GameFrameType, 0, 0, [1]));
        PumpUntil(bridge, () => gameFrames == 1);
        Check.That(bridge.IsReady && gameFrames == 1, "a second connection without HELLO leaves the linked adapter up");
        bridge.Send(new Frame(GameFrameType, Framing.FlagReliable, 0, [2]));
        Check.That(first.ReadOf(GameFrameType) != null, "frames still go to the linked adapter");

        using var wrongGame = new Adapter(bridge.Port);
        wrongGame.Hello("other");
        PumpFor(bridge, 200);
        Check.That(wrongGame.ReadOf(Msg.Reject) != null, "a HELLO for another game is rejected with a REJECT frame");
        Check.That(bridge.IsReady && ready == 1, "a rejected connection leaves the linked adapter up");

        using var second = new Adapter(bridge.Port);
        second.Hello(GameId);
        PumpUntil(bridge, () => ready == 2);
        Check.That(ready == 2 && bridge.IsReady, "a second connection with a valid HELLO becomes the link");
        Check.That(first.ClosedByBridge(), "the replaced adapter connection is closed");
        bridge.Send(new Frame(GameFrameType, Framing.FlagReliable, 0, [3]));
        Check.That(second.ReadOf(GameFrameType) != null, "frames go to the new link");
    }

    private static void PendingConnectionWithoutHelloIsClosed()
    {
        using var bridge = new LoopbackBridge(Profile, 0, ShortLinkTimeoutMs);
        using var silent = new Adapter(bridge.Port);
        PumpFor(bridge, ShortLinkTimeoutMs * 3);
        Check.That(silent.ClosedByBridge(), "a connection that sends no HELLO is closed");
        Check.That(!bridge.IsReady, "a closed pending connection never became the link");
    }

    private static void FloodOfUnreliableFramesNeverBlocksSend()
    {
        using var bridge = new LoopbackBridge(Profile, 0);
        using var adapter = new Adapter(bridge.Port);
        adapter.Hello(GameId);
        PumpUntil(bridge, () => bridge.IsReady);

        var frame = new Frame(GameFrameType, 0, 0, new byte[FloodPayloadBytes]);
        var watch = Stopwatch.StartNew();
        for (var i = 0; i < FloodFrames; i++) bridge.Send(frame);
        var elapsed = watch.ElapsedMilliseconds;
        bridge.Pump();
        Check.That(elapsed < NeverBlockedMs, $"{FloodFrames} sends to a reader that does not read took {elapsed} ms: Send never blocks");
        Check.That(bridge.DroppedUnreliable > 0, "unreliable frames are dropped once the queue backs up");
        Check.That(bridge.IsReady, "dropping unreliable frames leaves the link up");
    }

    private static void OptionSetReachesTheEventAndLobbyStateTheAdapter()
    {
        using var bridge = new LoopbackBridge(Profile, 0);
        var payloads = new List<byte[]>();
        var gameFrames = 0;
        bridge.OptionSet += payloads.Add;
        bridge.GameFrame += _ => gameFrames++;
        using var adapter = new Adapter(bridge.Port);
        adapter.Hello(GameId);
        PumpUntil(bridge, () => bridge.IsReady);

        var set = Encoding.ASCII.GetBytes("outfit_rebecca=7d");
        adapter.Send(Framing.Reliable(Msg.OptionSet, set));
        PumpUntil(bridge, () => payloads.Count == 1);
        Check.That(payloads.Count == 1 && payloads[0].SequenceEqual(set) && gameFrames == 0, "OPTION_SET from the adapter raises the event with its payload and is not a game frame");

        const string text = "member\t0\t1\t1\trebecca\tRebecca\tWestern\tPrécis";
        bridge.Send(ControlMessages.LobbyState(text));
        var state = adapter.ReadOf(Msg.LobbyState);
        Check.That(state is { } frame && (frame.Flags & Framing.FlagReliable) != 0 && Encoding.UTF8.GetString(frame.Payload) == text, "LOBBY_STATE reaches the adapter as a reliable UTF-8 frame");
    }

    private static void BackedUpReliableFramesDropTheLink()
    {
        using var bridge = new LoopbackBridge(Profile, 0);
        using var adapter = new Adapter(bridge.Port);
        adapter.Hello(GameId);
        PumpUntil(bridge, () => bridge.IsReady);

        var frame = new Frame(GameFrameType, Framing.FlagReliable, 0, new byte[ReliablePayloadBytes]);
        var watch = Stopwatch.StartNew();
        for (var i = 0; i < ReliableFrames; i++) bridge.Send(frame);
        Check.That(watch.ElapsedMilliseconds < NeverBlockedMs, "reliable sends to a reader that does not read do not block either");
        Check.That(!bridge.IsReady, "reliable frames backed up past the limit drop the link");
    }
}
