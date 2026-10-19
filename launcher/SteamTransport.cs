using System.Runtime.InteropServices;
using Steamworks;

namespace CoopLauncher;

/// <summary>ISteamNetworkingMessages on the Main channel (0) and the Fresh channel (1), both received. Sessions are
/// accepted only from lobby members.</summary>
public sealed class SteamTransport : ITransport
{
    private const int MaxMessagesPerPump = 64;
    private const int SteamChunkBytes = 64 * 1024;  // keeps several chunks within Steam's default 512 KiB send buffer
    private const int ReliableFlags = Constants.k_nSteamNetworkingSend_Reliable | Constants.k_nSteamNetworkingSend_NoNagle |
                                      Constants.k_nSteamNetworkingSend_AutoRestartBrokenSession;
    private const int UnreliableFlags = Constants.k_nSteamNetworkingSend_Unreliable | Constants.k_nSteamNetworkingSend_NoNagle |
                                        Constants.k_nSteamNetworkingSend_AutoRestartBrokenSession;
    private static readonly TransportChannel[] ReceiveChannels = [TransportChannel.Main, TransportChannel.Fresh];

    private readonly Func<ulong, bool> _isLobbyMember;
    private readonly Callback<SteamNetworkingMessagesSessionRequest_t> _sessionRequest;
    private readonly Callback<SteamNetworkingMessagesSessionFailed_t> _sessionFailed;
    private readonly IntPtr[] _messages = new IntPtr[MaxMessagesPerPump];

    public SteamTransport(Func<ulong, bool> isLobbyMember)
    {
        _isLobbyMember = isLobbyMember;
        _sessionRequest = Callback<SteamNetworkingMessagesSessionRequest_t>.Create(OnSessionRequest);
        _sessionFailed = Callback<SteamNetworkingMessagesSessionFailed_t>.Create(
            r => Log.Info($"Steam session to {r.m_info.m_identityRemote.GetSteamID64()} failed: {r.m_info.m_eEndReason}"));
        LocalId = SteamUser.GetSteamID().m_SteamID;
    }

    public ulong LocalId { get; }

    public int MaxChunkBytes => SteamChunkBytes;

    public event Action<ulong, Frame>? FrameReceived;

    public bool Send(ulong peerId, Frame frame, TransportChannel channel = TransportChannel.Main)
    {
        var data = Framing.EncodeWire(frame);
        var identity = new SteamNetworkingIdentity();
        identity.SetSteamID64(peerId);
        var flags = (frame.Flags & Framing.FlagReliable) != 0 ? ReliableFlags : UnreliableFlags;
        var pin = GCHandle.Alloc(data, GCHandleType.Pinned);
        try
        {
            return SteamNetworkingMessages.SendMessageToUser(ref identity, pin.AddrOfPinnedObject(), (uint)data.Length, flags, (int)channel) == EResult.k_EResultOK;
        }
        finally
        {
            pin.Free();
        }
    }

    public void Pump()
    {
        foreach (var channel in ReceiveChannels) PumpChannel((int)channel);
    }

    private void PumpChannel(int channel)
    {
        var count = SteamNetworkingMessages.ReceiveMessagesOnChannel(channel, _messages, _messages.Length);
        for (var i = 0; i < count; i++)
        {
            var msg = Marshal.PtrToStructure<SteamNetworkingMessage_t>(_messages[i]);
            var data = new byte[msg.m_cbSize];
            Marshal.Copy(msg.m_pData, data, 0, data.Length);
            var sender = msg.m_identityPeer.GetSteamID64();
            SteamNetworkingMessage_t.Release(_messages[i]);
            if (Framing.TryDecodeWire(data, out var frame)) FrameReceived?.Invoke(sender, frame);
        }
    }

    public void Dispose()
    {
        _sessionRequest.Dispose();
        _sessionFailed.Dispose();
    }

    private void OnSessionRequest(SteamNetworkingMessagesSessionRequest_t request)
    {
        var remote = request.m_identityRemote;
        var id = remote.GetSteamID64();
        if (!_isLobbyMember(id))
        {
            Log.Info($"Ignoring session request from non-member {id}");
            return;
        }
        SteamNetworkingMessages.AcceptSessionWithUser(ref remote);
    }
}
