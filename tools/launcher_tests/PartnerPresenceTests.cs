using System.Net;
using System.Net.Sockets;
using System.Text;
using CoopLauncher;

namespace LauncherTests;

/// <summary>A partner whose game left while its launcher stays in the lobby: the step it publishes decides when the adapter hears
/// PEER_DOWN and PEER_UP, once per change.</summary>
public static class PartnerPresenceTests
{
    private const ulong HostId = 1;
    private const ulong GuestId = 2;
    private const int ReadTimeoutMs = 300;
    private const int FirstPartnerSlot = 1;

    private static readonly GameProfile Profile = new("t", "Test", 1, "t.exe", 4, 1, [], "coop/adapter.ini");

    public static void Run()
    {
        StepsDecideTheChange();
        AGameStillGoneIsToldAgainAfterTheAdapterWasToldUp();
        AdapterHearsEachChangeOnce();
    }

    private static string Text(SessionStep step) => StepCodec.Encode(new StepReport(step));

    private static void StepsDecideTheChange()
    {
        var presence = new PartnerPresence();
        Check.That(presence.Observe(GuestId, string.Empty) == PresenceChange.None, "presence: a member that publishes nothing changes nothing");
        Check.That(presence.Observe(GuestId, Text(SessionStep.Lobby)) == PresenceChange.None, "presence: Lobby before any game is not a leave");
        Check.That(presence.Observe(GuestId, Text(SessionStep.InGame)) == PresenceChange.None, "presence: a first InGame is no return");
        Check.That(presence.Observe(GuestId, Text(SessionStep.InGame)) == PresenceChange.None, "presence: the same step again changes nothing");
        Check.That(presence.Observe(GuestId, Text(SessionStep.GameClosed)) == PresenceChange.Down, "presence: GameClosed is a leave");
        Check.That(presence.Observe(GuestId, Text(SessionStep.GameClosed) + ";x=1") == PresenceChange.None, "presence: a leave is reported once");
        Check.That(presence.Observe(GuestId, Text(SessionStep.StartingGame)) == PresenceChange.None, "presence: a game starting again is not back yet");
        Check.That(presence.Observe(GuestId, Text(SessionStep.InGame)) == PresenceChange.Up, "presence: InGame after a leave is a return");
        Check.That(presence.Observe(GuestId, Text(SessionStep.Refused)) == PresenceChange.Down, "presence: Refused after InGame is a leave");
        Check.That(presence.Observe(GuestId, Text(SessionStep.Lobby)) == PresenceChange.None, "presence: Lobby after that is the same leave");
        Check.That(presence.Observe(GuestId, Text(SessionStep.StartingGame)) == PresenceChange.None && presence.Observe(GuestId, Text(SessionStep.Lobby)) == PresenceChange.None,
            "presence: a game that never came back is not reported again");
        presence.Observe(GuestId, Text(SessionStep.InGame));
        Check.That(presence.Observe(GuestId, Text(SessionStep.StartingGame)) == PresenceChange.None && presence.Observe(GuestId, Text(SessionStep.Lobby)) == PresenceChange.Down,
            "presence: Lobby after StartingGame is a leave");
        Check.That(presence.Observe(GuestId, Text(SessionStep.None)) == PresenceChange.None, "presence: step None says nothing");
    }

    private static void AGameStillGoneIsToldAgainAfterTheAdapterWasToldUp()
    {
        var presence = new PartnerPresence();
        presence.Observe(GuestId, Text(SessionStep.InGame));
        presence.Observe(GuestId, Text(SessionStep.Lobby));
        presence.AdapterToldUp(GuestId);
        Check.That(presence.Observe(GuestId, Text(SessionStep.Lobby)) == PresenceChange.Down, "presence: a partner brought back by the lobby whose game is gone is reported again");
        presence.Forget(GuestId);
        Check.That(presence.Observe(GuestId, Text(SessionStep.Lobby)) == PresenceChange.None, "presence: a forgotten partner starts over");
    }

    private static void AdapterHearsEachChangeOnce()
    {
        var lobby = new ScriptedLobby(HostId, HostId, GuestId);
        var transport = new ScriptedTransport(HostId);
        using var bridge = new LoopbackBridge(Profile, 0);
        using var client = new TcpClient();
        client.Connect(IPAddress.Loopback, bridge.Port);
        client.ReceiveTimeout = ReadTimeoutMs;
        var hello = new byte[3 + Profile.Id.Length];
        BitConverter.TryWriteBytes(hello, Framing.ProtocolVersion);
        hello[2] = (byte)Profile.Id.Length;
        Encoding.ASCII.GetBytes(Profile.Id).CopyTo(hello, 3);
        client.GetStream().Write(Framing.EncodeLoopback(new Frame(Msg.Hello, Framing.FlagReliable, 0, hello)));
        using var session = new Session(Profile, lobby, transport, bridge);
        while (!bridge.IsReady) { bridge.Pump(); Thread.Sleep(1); }
        session.Pump();
        Drain(client);

        lobby.SetStep(GuestId, SessionStep.InGame);
        Pump(session, bridge);
        Check.That(Drain(client).Count == 0, "adapter: a partner in game tells it nothing");
        lobby.SetStep(GuestId, SessionStep.GameClosed);
        Pump(session, bridge);
        Check.That(Drain(client).SequenceEqual([(Msg.PeerDown, (byte)FirstPartnerSlot)]), "adapter: a partner whose game closed is PEER_DOWN, once");
        lobby.SetStep(GuestId, SessionStep.StartingGame);
        Pump(session, bridge);
        Check.That(Drain(client).Count == 0, "adapter: a partner starting its game again tells it nothing yet");
        lobby.SetStep(GuestId, SessionStep.InGame);
        Pump(session, bridge);
        Check.That(Drain(client).SequenceEqual([(Msg.PeerUp, (byte)FirstPartnerSlot)]), "adapter: a partner back in game is PEER_UP, once");
    }

    private static void Pump(Session session, LoopbackBridge bridge)
    {
        for (var i = 0; i < 3; i++)
        {
            session.Pump();
            bridge.Pump();
        }
    }

    private static List<(ushort Type, byte Slot)> Drain(TcpClient client)
    {
        var seen = new List<(ushort, byte)>();
        while (Framing.TryReadLoopback(client.GetStream(), out var frame))
            if (frame.Type is Msg.PeerUp or Msg.PeerDown) seen.Add((frame.Type, frame.Payload[0]));
        return seen;
    }
}
