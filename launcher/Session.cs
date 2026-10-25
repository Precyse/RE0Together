namespace CoopLauncher;

/// <summary>
/// Turns lobby membership into slots and epochs, and routes frames between adapter and peers. A member that vanishes from
/// the lobby is held with its slot and frames before PEER_DOWN (<see cref="MemberGrace.PeerGraceMs"/>; the host
/// <see cref="MemberGrace.HostGraceMs"/>, so a guest keeps its session while the host's lobby is gone); one that comes
/// back in time gets PEER_UP again for the same slot (the adapter resyncs). The session itself never ends because the host
/// is missing from the lobby: the app sees <see cref="HostMissing"/> and decides.
/// </summary>
public sealed class Session : IDisposable
{
    private const long BudgetReportIntervalMs = 5000;
    private const string DefaultName = "A player";

    private readonly GameProfile _profile;
    private readonly ITransport _transport;
    private readonly LoopbackBridge _bridge;
    private readonly PeerStats _stats = new();
    private readonly MemberGrace _grace = new();
    private readonly PeerBudget _budget = new();
    private readonly FreshQueue? _fresh;
    private readonly Dictionary<ulong, string> _names = new();

    private ILobby _lobby;
    private ulong _lobbyId;
    private Dictionary<ulong, byte> _slots = new();
    private ulong _hostId;
    private byte _localSlot;
    private uint _epoch;
    private bool _active;
    private bool _recheck;
    private int _seenRevision = -1;
    private long _lastBudgetReportMs;
    private long _lastFreshReplaced;

    public Session(GameProfile profile, ILobby lobby, ITransport transport, LoopbackBridge bridge)
    {
        _profile = profile;
        _lobby = lobby;
        _lobbyId = lobby.Id;
        _hostId = lobby.OwnerId;
        _transport = transport;
        _bridge = bridge;
        _fresh = FreshQueue.ForProfile(profile);
        if (_fresh != null) Log.Info($"Fresh channel: {_fresh.TypeCount} types");
        _transport.FrameReceived += OnPeerFrame;
        _bridge.GameFrame += OnAdapterFrame;
        _bridge.AdapterReady += OnAdapterReady;
        _bridge.SaveChanged += () => SaveChanged?.Invoke();
        _bridge.OptionSet += payload => OptionSet?.Invoke(payload);
    }

    public bool Ended { get; private set; }

    /// <summary>The guest's lobby object failed: membership is frozen and every peer is held until a lobby is rebound.</summary>
    public bool Held { get; private set; }

    public ulong HostId => _hostId;

    public bool IsHost => _hostId == _transport.LocalId;

    public ulong LobbyId => _lobbyId;

    public bool PeerConnected => _active && !Ended && _slots.Count > 1;

    /// <summary>A guest's host is no longer in the lobby, or no longer owns it (outside <see cref="Held"/>, when the lobby
    /// object itself is gone).</summary>
    public bool HostMissing =>
        _active && !Ended && !Held && !IsHost && (_lobby.OwnerId != _hostId || _lobby.Members.All(member => member.Id != _hostId));

    public int? RttMs => _stats.RttMs;

    /// <summary>Raised on the main loop for each non-local peer that appears in the slot map, and for a held one that comes back.</summary>
    public event Action<ulong>? PeerJoined;

    /// <summary>Launcher-owned file transfer frames (FILE_*), with the sender's transport id. Never relayed to the adapter.</summary>
    public event Action<ulong, Frame>? FileFrameReceived;

    /// <summary>The local adapter reports that the game wrote a save file (SAVE_CHANGED).</summary>
    public event Action? SaveChanged;

    /// <summary>The local adapter reports an option the player changed on the game's own co-op page (OPTION_SET), with the payload.</summary>
    public event Action<byte[]>? OptionSet;

    /// <summary>Transport ids of the other members in the slot map.</summary>
    public IReadOnlyList<ulong> PeerIds => _slots.Keys.Where(id => id != _transport.LocalId).ToList();

    /// <summary>A peer's build number (BUILD_INFO), with the sender's transport id.</summary>
    public event Action<ulong, Frame>? BuildInfoReceived;

    /// <summary>A guest's diagnostics (LOG_APPEND), with the sender's transport id. Never relayed to the adapter.</summary>
    public event Action<ulong, Frame>? DiagnosticsFrameReceived;

    /// <summary>True for another member of the slot map, also while it is held.</summary>
    public bool HasPeer(ulong id) => id != _transport.LocalId && _slots.ContainsKey(id);

    /// <summary>A member's session slot (the local member's is the local slot), null while the slot map does not have it.</summary>
    public byte? SlotOf(ulong memberId) => _slots.TryGetValue(memberId, out var slot) ? slot : null;

    /// <summary>A member's round-trip time, null before its first pong (and for the local member).</summary>
    public int? RttOf(ulong memberId) => PeerSlot(memberId) is { } slot ? _stats.RttOf(slot) : null;

    /// <summary>Starts a member's round-trip time again from the next ping (see <see cref="PeerStats.Restart"/>).</summary>
    public void RestartRtt(ulong memberId)
    {
        if (PeerSlot(memberId) is { } slot) _stats.Restart(slot);
    }

    /// <summary>True when a member that is not held has not answered a ping for <see cref="PeerStats.SilentAfterMs"/>.</summary>
    public bool IsSilent(ulong memberId, long nowMs) => PeerSlot(memberId) is { } slot && _stats.SilentFor(slot, nowMs);

    /// <summary>True while a member is missing from the lobby and still inside its grace.</summary>
    public bool IsHeld(ulong memberId) => _grace.HeldIds.Contains(memberId);

    /// <summary>Milliseconds before a held member is given up, 0 when it is not held.</summary>
    public long HoldMsLeft(ulong memberId, long nowMs) => _grace.MsLeft(memberId, nowMs, GraceFor);

    public void Pump()
    {
        if (Ended) return;
        var now = Environment.TickCount64;
        if (_recheck || (!Held && _lobby.Revision != _seenRevision) || _grace.ExpiryDue(now, GraceFor))
        {
            _recheck = false;
            if (!Held) _seenRevision = _lobby.Revision;
            ApplyMembership(now);
        }
        if (_active && !Ended)
            _stats.Tick(PeerSlots(), SendPing, (slot, rtt) => _bridge.Send(ControlMessages.PeerStats(slot, rtt)));
        FlushFresh();
        ReportBudget(now);
    }

    /// <summary>The guest's lobby object failed. The session, its slots and frames carry on while every peer is held for the
    /// grace. False when there is nothing to hold (the session never became active, or it ended).</summary>
    public bool Hold()
    {
        if (!_active || Ended) return false;
        if (!Held) Log.Info("Lobby lost, holding the session");
        Held = true;
        _recheck = true;
        return true;
    }

    /// <summary>Gives up every held partner now (the host plays on alone): the next pump sends them PEER_DOWN.</summary>
    public void ReleaseHeld()
    {
        _grace.ExpireAll();
        _recheck = true;
    }

    /// <summary>A lobby replaced the failed one: membership follows it again, and the adapter hears every peer come up again
    /// (it was unreachable for a while, so it resyncs).</summary>
    public void Rebind(ILobby lobby)
    {
        _lobby = lobby;
        _lobbyId = lobby.Id;
        _seenRevision = -1;
        Held = false;
        _grace.Clear();
        Log.Info("Session rebound to the new lobby");
        foreach (var (id, slot) in _slots)
            if (id != _transport.LocalId) SendPeerUp(id, slot);
    }

    public void Dispose() => _transport.FrameReceived -= OnPeerFrame;

    private long GraceFor(ulong memberId) => memberId == _hostId ? MemberGrace.HostGraceMs : MemberGrace.PeerGraceMs;

    private byte? PeerSlot(ulong memberId) =>
        memberId != _transport.LocalId && _slots.TryGetValue(memberId, out var slot) ? slot : null;

    private string NameOf(ulong memberId) =>
        _names.TryGetValue(memberId, out var name) && name.Length > 0 ? name : DefaultName;

    private List<byte> PeerSlots() =>
        _slots.Where(p => p.Key != _transport.LocalId).Select(p => p.Value).ToList();

    private void ApplyMembership(long nowMs)
    {
        var present = Held ? new HashSet<ulong>() : _lobby.Members.Select(m => m.Id).ToHashSet();
        if (!Held)
            foreach (var member in _lobby.Members) _names[member.Id] = member.Name;
        if (!_active)
        {
            Activate(present);
            return;
        }
        var heldBefore = _grace.HeldIds.ToHashSet();
        var verdict = _grace.Update(PeerIds, present, nowMs, GraceFor);
        if (!Held) LogHolds(verdict, heldBefore);
        if (verdict.Expired.Contains(_hostId))
        {
            End("host left");
            return;
        }
        var next = SlotAssigner.Assign(_hostId, present.Concat(verdict.Held).Append(_transport.LocalId), _profile.MaxPlayers);
        if (!next.TryGetValue(_transport.LocalId, out var mySlot))
        {
            End("no slot available");
            return;
        }
        if (mySlot != _localSlot) Reset(next, mySlot, _epoch + 1);
        else UpdatePeers(next, verdict.Returned);
    }

    /// <summary>The partners' side of the story: who just went missing and who came back. While the whole session is held it is
    /// this launcher that lost the lobby, not the partners, so nothing is said about them.</summary>
    private void LogHolds(GraceVerdict verdict, IReadOnlySet<ulong> heldBefore)
    {
        foreach (var id in verdict.Held.Where(id => !heldBefore.Contains(id))) Log.Event($"{NameOf(id)} reconnecting");
        foreach (var id in verdict.Returned) Log.Event($"{NameOf(id)} back");
    }

    private void Activate(HashSet<ulong> present)
    {
        if (!present.Contains(_lobby.OwnerId)) return;
        var hostId = _lobby.OwnerId;
        var next = SlotAssigner.Assign(hostId, present, _profile.MaxPlayers);
        if (!next.TryGetValue(_transport.LocalId, out var mySlot))
        {
            End("no slot available");
            return;
        }
        _active = true;
        _hostId = hostId;
        Reset(next, mySlot, 1);
    }

    private void Reset(Dictionary<ulong, byte> slots, byte localSlot, uint epoch)
    {
        _slots = slots;
        _localSlot = localSlot;
        _epoch = epoch;
        Log.Info($"Session epoch {epoch}: local slot {localSlot}, {slots.Count} member(s)");
        if (_bridge.IsReady) SendSnapshot();
        foreach (var id in slots.Keys.Where(id => id != _transport.LocalId)) PeerJoined?.Invoke(id);
    }

    private void UpdatePeers(Dictionary<ulong, byte> next, IReadOnlyList<ulong> returned)
    {
        foreach (var (id, slot) in _slots)
        {
            if (id == _transport.LocalId || (next.TryGetValue(id, out var s) && s == slot)) continue;
            _stats.Remove(slot);
            _budget.Remove(slot);
            _bridge.Send(ControlMessages.PeerDown(slot));
            if (!next.ContainsKey(id)) Log.Event($"{NameOf(id)} left");
        }
        var previous = _slots;
        _slots = next;
        foreach (var (id, slot) in next)
        {
            if (id == _transport.LocalId) continue;
            var isNew = !previous.ContainsKey(id);
            if (returned.Contains(id)) _stats.Remove(slot);
            if (!returned.Contains(id) && !isNew && previous[id] == slot) continue;
            if (isNew) Log.Event($"{NameOf(id)} joined");
            SendPeerUp(id, slot);
            PeerJoined?.Invoke(id);
        }
    }

    private void OnAdapterReady()
    {
        if (_active) SendSnapshot();
    }

    private void SendSnapshot()
    {
        _bridge.Send(ControlMessages.Welcome(_localSlot, SlotAssigner.HostSlot, (byte)_profile.MaxPlayers, _epoch, _transport.LocalId));
        foreach (var (id, slot) in _slots)
            if (id != _transport.LocalId) SendPeerUp(id, slot);
    }

    private void SendPeerUp(ulong id, byte slot) =>
        _bridge.Send(ControlMessages.PeerUp(slot, id, _names.GetValueOrDefault(id, id.ToString())));

    private void ReportBudget(long nowMs)
    {
        if (nowMs - _lastBudgetReportMs < BudgetReportIntervalMs) return;
        _lastBudgetReportMs = nowMs;
        foreach (var line in _budget.TakeReport()) Log.Info(line);
        if (_fresh != null && _fresh.Replaced != _lastFreshReplaced)
        {
            _lastFreshReplaced = _fresh.Replaced;
            Log.Info($"Fresh channel: {_lastFreshReplaced} older frames replaced");
        }
    }

    private void End(string reason)
    {
        Log.Info($"Session ended: {reason}");
        _epoch++;
        foreach (var (id, slot) in _slots)
            if (id != _transport.LocalId) _bridge.Send(ControlMessages.PeerDown(slot));
        Ended = true;
    }

    private Frame Outgoing(Frame frame) =>
        frame with { Flags = (byte)(frame.Flags & Framing.FlagReliable), Slot = _localSlot };

    private void SendToPeer(ulong id, Frame frame) => _transport.Send(id, Outgoing(frame));

    /// <summary>A fresh type waits for the next flush (newest wins); everything else goes out now.</summary>
    private void RouteToPeer(ulong id, Frame frame)
    {
        if (_fresh?.Accepts(frame) == true) _fresh.Enqueue(id, Outgoing(frame));
        else SendToPeer(id, frame);
    }

    private void FlushFresh()
    {
        if (_fresh == null) return;
        foreach (var (id, frame) in _fresh.Drain()) _transport.Send(id, frame, TransportChannel.Fresh);
    }

    private void SendPing(byte slot)
    {
        if (TryGetPeerId(slot, out var id)) SendToPeer(id, ControlMessages.Ping(PeerStats.NowMicros));
    }

    private bool TryGetPeerId(byte slot, out ulong id)
    {
        foreach (var (peerId, peerSlot) in _slots)
            if (peerSlot == slot && peerId != _transport.LocalId)
            {
                id = peerId;
                return true;
            }
        id = 0;
        return false;
    }

    private void OnAdapterFrame(Frame frame)
    {
        if (!_active) return;
        if (frame.Slot == Framing.SlotAll)
        {
            foreach (var id in _slots.Keys.Where(id => id != _transport.LocalId).ToList()) RouteToPeer(id, frame);
        }
        else if (TryGetPeerId(frame.Slot, out var id)) RouteToPeer(id, frame);
    }

    private void OnPeerFrame(ulong senderId, Frame frame)
    {
        if (Msg.IsFileTransfer(frame.Type))
        {
            FileFrameReceived?.Invoke(senderId, frame);
            return;
        }
        if (frame.Type == Msg.BuildInfo)
        {
            BuildInfoReceived?.Invoke(senderId, frame);
            return;
        }
        if (frame.Type == Msg.LogAppend)
        {
            DiagnosticsFrameReceived?.Invoke(senderId, frame);
            return;
        }
        if (!_active || !_slots.TryGetValue(senderId, out var slot)) return;
        frame = frame with { Slot = slot };
        if (frame.Type >= Msg.FirstGameType)
        {
            if (_budget.Admit(slot, frame, Environment.TickCount64)) _bridge.Send(frame);
        }
        else if (frame.Type == Msg.Ping && ControlMessages.TryParseTimestamp(frame.Payload, out var ping))
            SendToPeer(senderId, ControlMessages.Pong(ping));
        else if (frame.Type == Msg.Pong && ControlMessages.TryParseTimestamp(frame.Payload, out var echoed))
            _stats.OnPong(slot, echoed);
    }
}
