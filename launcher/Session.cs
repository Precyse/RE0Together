namespace CoopLauncher;

/// <summary>Turns lobby membership into slots and epochs, and routes frames between adapter and peers.</summary>
public sealed class Session : IDisposable
{
    private readonly GameProfile _profile;
    private readonly ILobby _lobby;
    private readonly ITransport _transport;
    private readonly LoopbackBridge _bridge;
    private readonly PeerStats _stats = new();

    private Dictionary<ulong, byte> _slots = new();
    private ulong _hostId;
    private byte _localSlot;
    private uint _epoch;
    private bool _active;
    private int _seenRevision = -1;

    public Session(GameProfile profile, ILobby lobby, ITransport transport, LoopbackBridge bridge)
    {
        _profile = profile;
        _lobby = lobby;
        _transport = transport;
        _bridge = bridge;
        _transport.FrameReceived += OnPeerFrame;
        _bridge.GameFrame += OnAdapterFrame;
        _bridge.AdapterReady += OnAdapterReady;
    }

    public bool Ended { get; private set; }

    public bool PeerConnected => _active && !Ended && _slots.Count > 1;

    public int? RttMs => _stats.RttMs;

    /// <summary>Raised on the main loop for each non-local peer that appears in the slot map.</summary>
    public event Action<ulong>? PeerJoined;

    /// <summary>Launcher-owned file transfer frames (FILE_*), with the sender's transport id. Never relayed to the adapter.</summary>
    public event Action<ulong, Frame>? FileFrameReceived;

    /// <summary>A guest's adapter log lines (LOG_APPEND), with the sender's transport id. Never relayed to the adapter.</summary>
    public event Action<ulong, Frame>? LogFrameReceived;

    public void Pump()
    {
        if (Ended) return;
        if (_lobby.Revision != _seenRevision)
        {
            _seenRevision = _lobby.Revision;
            ApplyMembership();
        }
        if (_active && !Ended)
            _stats.Tick(PeerSlots(), SendPing, (slot, rtt) => _bridge.Send(ControlMessages.PeerStats(slot, rtt)));
    }

    public void Dispose() => _transport.FrameReceived -= OnPeerFrame;

    private List<byte> PeerSlots() =>
        _slots.Where(p => p.Key != _transport.LocalId).Select(p => p.Value).ToList();

    private void ApplyMembership()
    {
        var members = _lobby.Members;
        var ownerPresent = members.Any(m => m.Id == _lobby.OwnerId);
        if (!_active && !ownerPresent) return;
        if (_active && (!ownerPresent || _lobby.OwnerId != _hostId))
        {
            End("host left");
            return;
        }

        var hostId = _active ? _hostId : _lobby.OwnerId;
        var next = SlotAssigner.Assign(hostId, members.Select(m => m.Id), _profile.MaxPlayers);
        if (!next.TryGetValue(_transport.LocalId, out var mySlot))
        {
            End("no slot available");
            return;
        }

        if (!_active)
        {
            _active = true;
            _hostId = hostId;
            Reset(next, mySlot, 1);
        }
        else if (mySlot != _localSlot) Reset(next, mySlot, _epoch + 1);
        else UpdatePeers(next);
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

    private void UpdatePeers(Dictionary<ulong, byte> next)
    {
        foreach (var (id, slot) in _slots)
        {
            if (id == _transport.LocalId || (next.TryGetValue(id, out var s) && s == slot)) continue;
            _stats.Remove(slot);
            _bridge.Send(ControlMessages.PeerDown(slot));
        }
        var previous = _slots;
        _slots = next;
        foreach (var (id, slot) in next)
            if (id != _transport.LocalId && !(previous.TryGetValue(id, out var s) && s == slot))
            {
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
        _bridge.Send(ControlMessages.Welcome(_localSlot, SlotAssigner.HostSlot, (byte)_profile.MaxPlayers, _epoch));
        foreach (var (id, slot) in _slots)
            if (id != _transport.LocalId) SendPeerUp(id, slot);
    }

    private void SendPeerUp(ulong id, byte slot)
    {
        var name = _lobby.Members.FirstOrDefault(m => m.Id == id)?.Name ?? id.ToString();
        _bridge.Send(ControlMessages.PeerUp(slot, id, name));
    }

    private void End(string reason)
    {
        Log.Info($"Session ended: {reason}");
        _epoch++;
        foreach (var (id, slot) in _slots)
            if (id != _transport.LocalId) _bridge.Send(ControlMessages.PeerDown(slot));
        Ended = true;
    }

    private void SendToPeer(ulong id, Frame frame) =>
        _transport.Send(id, frame with { Flags = (byte)(frame.Flags & Framing.FlagReliable), Slot = _localSlot });

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
            foreach (var id in _slots.Keys.Where(id => id != _transport.LocalId).ToList()) SendToPeer(id, frame);
        }
        else if (TryGetPeerId(frame.Slot, out var id)) SendToPeer(id, frame);
    }

    private void OnPeerFrame(ulong senderId, Frame frame)
    {
        if (Msg.IsFileTransfer(frame.Type))
        {
            FileFrameReceived?.Invoke(senderId, frame);
            return;
        }
        if (frame.Type == Msg.LogAppend)
        {
            LogFrameReceived?.Invoke(senderId, frame);
            return;
        }
        if (!_active || !_slots.TryGetValue(senderId, out var slot)) return;
        frame = frame with { Slot = slot };
        if (frame.Type >= Msg.FirstGameType) _bridge.Send(frame);
        else if (frame.Type == Msg.Ping && ControlMessages.TryParseTimestamp(frame.Payload, out var ping))
            SendToPeer(senderId, ControlMessages.Pong(ping));
        else if (frame.Type == Msg.Pong && ControlMessages.TryParseTimestamp(frame.Payload, out var echoed))
            _stats.OnPong(slot, echoed);
    }
}
