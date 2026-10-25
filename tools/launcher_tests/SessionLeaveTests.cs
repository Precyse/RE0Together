using CoopLauncher;

namespace LauncherTests;

/// <summary>An ITransport with a chosen local id that records what is sent and lets a test deliver a frame.</summary>
internal sealed class ScriptedTransport(ulong localId) : ITransport
{
    public List<(ulong Peer, Frame Frame)> Sent { get; } = [];

    public ulong LocalId { get; } = localId;

    public int MaxChunkBytes => 1024;

    public event Action<ulong, Frame>? FrameReceived;

    public bool Send(ulong peerId, Frame frame, TransportChannel channel = TransportChannel.Main)
    {
        Sent.Add((peerId, frame));
        return true;
    }

    public void Receive(ulong sender, ushort type) => FrameReceived?.Invoke(sender, Framing.Reliable(type, []));

    public int CountTo(ulong peer, ushort type) => Sent.Count(sent => sent.Peer == peer && sent.Frame.Type == type);

    public void Pump()
    {
    }

    public void Dispose()
    {
    }
}

/// <summary>A lobby whose members the test sets.</summary>
internal sealed class ScriptedLobby(ulong ownerId, params ulong[] members) : ILobby
{
    private IReadOnlyList<LobbyMember> _members = members.Select(id => new LobbyMember(id, $"Player {id}")).ToList();

    public bool IsReady => true;

    public LobbyFailure? Failure => null;

    public string? GameId => "t";

    public ulong Id => 1;

    public ulong OwnerId { get; } = ownerId;

    public IReadOnlyDictionary<string, string> HostOptions { get; } = new Dictionary<string, string>();

    public string JoinSecret => string.Empty;

    public IReadOnlyList<LobbyMember> Members => _members;

    public int Revision { get; private set; }

    public LobbyListing? Listing => null;

    public void SetMembers(params ulong[] ids)
    {
        _members = ids.Select(id => new LobbyMember(id, $"Player {id}")).ToList();
        Revision++;
    }

    public void SetLobbyData(LobbyKey key, string value)
    {
    }

    public void SetMemberData(MemberKey key, string value)
    {
    }

    public string MemberData(LobbyMember member, MemberKey key) => string.Empty;

    public void Pump()
    {
    }

    public void Dispose()
    {
    }
}

/// <summary>Leaving on purpose: LEAVING drops a guest at once (a lost connection is held for its grace), a host's LEAVING or KICK ends a
/// guest's session with the reason, and the host's Kick removes a guest and keeps it out.</summary>
public static class SessionLeaveTests
{
    private const ulong HostId = 1;
    private const ulong GuestId = 2;
    private const ulong OtherGuestId = 3;

    private static readonly GameProfile Profile = new("t", "Test", 1, "t.exe", 4, 1, [], "coop/adapter.ini");

    public static void Run()
    {
        GuestLeavesOnPurpose();
        GuestConnectionLost();
        HostKicks();
        HostLeavesOnPurpose();
        HostRemovesGuest();
        AnnouncesLeaving();
    }

    private static Session Started(ScriptedLobby lobby, ScriptedTransport transport, LoopbackBridge bridge)
    {
        var session = new Session(Profile, lobby, transport, bridge);
        session.Pump();
        return session;
    }

    private static void GuestLeavesOnPurpose()
    {
        var lobby = new ScriptedLobby(HostId, HostId, GuestId);
        var transport = new ScriptedTransport(HostId);
        using var bridge = new LoopbackBridge(Profile, 0);
        using var session = Started(lobby, transport, bridge);
        Check.That(session.HasPeer(GuestId), "leave: the guest is a peer of the host's session");
        transport.Receive(GuestId, Msg.Leaving);
        session.Pump();
        Check.That(session.HasPeer(GuestId) && !session.Ended, "leave: LEAVING alone changes nothing while the lobby still lists the guest");
        lobby.SetMembers(HostId);
        session.Pump();
        Check.That(!session.HasPeer(GuestId) && !session.IsHeld(GuestId) && !session.Ended, "leave: once the lobby drops a guest that said it left it is gone at once, not held");
    }

    private static void GuestConnectionLost()
    {
        var lobby = new ScriptedLobby(HostId, HostId, GuestId);
        var transport = new ScriptedTransport(HostId);
        using var bridge = new LoopbackBridge(Profile, 0);
        using var session = Started(lobby, transport, bridge);
        lobby.SetMembers(HostId);
        session.Pump();
        Check.That(session.HasPeer(GuestId) && session.IsHeld(GuestId), "leave: a guest that vanished without saying so is held for its grace");
    }

    private static void HostKicks()
    {
        var lobby = new ScriptedLobby(HostId, HostId, GuestId, OtherGuestId);
        var transport = new ScriptedTransport(HostId);
        using var bridge = new LoopbackBridge(Profile, 0);
        using var session = Started(lobby, transport, bridge);
        Check.That(!session.Kick(99), "kick: someone who is not in the session cannot be removed");
        Check.That(session.Kick(GuestId) && !session.Kick(GuestId), "kick: a guest is removed once");
        Check.That(transport.CountTo(GuestId, Msg.Kick) == 1 && session.IsKicked(GuestId), "kick: the guest is told to leave");
        lobby.SetMembers(HostId, GuestId, OtherGuestId);
        session.Pump();
        Check.That(!session.HasPeer(GuestId) && !session.IsHeld(GuestId) && session.HasPeer(OtherGuestId), "kick: the removed guest is out of the slots at once, the other stays");
        Check.That(transport.CountTo(GuestId, Msg.Kick) == 2, "kick: a removed guest that is still a lobby member is told again when the lobby changes");
        Check.That(transport.CountTo(OtherGuestId, Msg.Kick) == 0, "kick: nobody else is told");
    }

    private static void HostLeavesOnPurpose()
    {
        var lobby = new ScriptedLobby(HostId, HostId, GuestId);
        var transport = new ScriptedTransport(GuestId);
        using var bridge = new LoopbackBridge(Profile, 0);
        using var session = Started(lobby, transport, bridge);
        transport.Receive(OtherGuestId, Msg.Leaving);
        transport.Receive(OtherGuestId, Msg.Kick);
        Check.That(!session.Ended, "leave: a guest's LEAVING or KICK from a member that is not the host changes nothing for a guest");
        transport.Receive(HostId, Msg.Leaving);
        Check.That(session.Ended && session.EndedBy == RefusalKind.HostLeft, "leave: the host's LEAVING ends a guest's session with Host left");
    }

    private static void HostRemovesGuest()
    {
        var lobby = new ScriptedLobby(HostId, HostId, GuestId);
        var transport = new ScriptedTransport(GuestId);
        using var bridge = new LoopbackBridge(Profile, 0);
        using var session = Started(lobby, transport, bridge);
        transport.Receive(HostId, Msg.Kick);
        Check.That(session.Ended && session.EndedBy == RefusalKind.Removed, "kick: the host's KICK ends the guest's session as Removed from the lobby");
        Check.That(!session.Kick(HostId), "kick: a guest cannot remove anyone");

        var hostSide = new ScriptedTransport(HostId);
        using var hostBridge = new LoopbackBridge(Profile, 0);
        using var host = Started(new ScriptedLobby(HostId, HostId, GuestId), hostSide, hostBridge);
        hostSide.Receive(GuestId, Msg.Kick);
        Check.That(!host.Ended && host.EndedBy == null, "kick: a KICK sent to the host is ignored");
    }

    private static void AnnouncesLeaving()
    {
        var transport = new ScriptedTransport(GuestId);
        using var bridge = new LoopbackBridge(Profile, 0);
        using var session = Started(new ScriptedLobby(HostId, HostId, GuestId, OtherGuestId), transport, bridge);
        session.AnnounceLeaving();
        Check.That(transport.CountTo(HostId, Msg.Leaving) == 1 && transport.CountTo(OtherGuestId, Msg.Leaving) == 1, "leave: a leaving launcher tells every peer");
        transport.Receive(HostId, Msg.Leaving);
        var before = transport.Sent.Count;
        session.AnnounceLeaving();
        Check.That(transport.Sent.Count == before, "leave: an ended session says nothing more");
    }
}
