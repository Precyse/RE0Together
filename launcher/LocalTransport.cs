using System.Net;
using System.Net.Sockets;

namespace CoopLauncher;

/// <summary>UDP on 127.0.0.1 between two launchers on one PC. Identity is the UDP port.</summary>
public sealed class LocalTransport : ITransport
{
    private const ushort PresenceType = 0x0030;
    private const int PresenceIntervalMs = 1000;
    private const int PresenceTimeoutMs = 3000;
    private const int UdpChunkBytes = 32 * 1024;
    private const int ReceiveBufferBytes = 8 * 1024 * 1024;

    private readonly UdpClient _socket;
    private readonly IPEndPoint _peerEndpoint;
    private long _lastPresenceSent;
    private long _lastRx = long.MinValue / 2;

    public LocalTransport(int localPort, int peerPort)
    {
        _socket = new UdpClient(new IPEndPoint(IPAddress.Loopback, localPort));
        _socket.Client.ReceiveBufferSize = ReceiveBufferBytes;
        _peerEndpoint = new IPEndPoint(IPAddress.Loopback, peerPort);
        LocalId = (ulong)localPort;
        PeerId = (ulong)peerPort;
    }

    public ulong LocalId { get; }

    public ulong PeerId { get; }

    public int MaxChunkBytes => UdpChunkBytes;

    /// <summary>True while datagrams from the peer port keep arriving.</summary>
    public bool PeerPresent => Environment.TickCount64 - _lastRx < PresenceTimeoutMs;

    public event Action<ulong, Frame>? FrameReceived;

    public bool Send(ulong peerId, Frame frame)
    {
        if (peerId != PeerId) return false;
        SendDatagram(Framing.EncodeWire(frame));
        return true;
    }

    public void Pump()
    {
        var now = Environment.TickCount64;
        if (now - _lastPresenceSent >= PresenceIntervalMs)
        {
            _lastPresenceSent = now;
            SendDatagram(Framing.EncodeWire(new Frame(PresenceType, 0, 0, [])));
        }
        while (TryReceive(out var data))
        {
            _lastRx = now;
            if (Framing.TryDecodeWire(data, out var frame) && frame.Type != PresenceType)
                FrameReceived?.Invoke(PeerId, frame);
        }
    }

    public void Dispose() => _socket.Dispose();

    private void SendDatagram(byte[] bytes)
    {
        try
        {
            _socket.Send(bytes, bytes.Length, _peerEndpoint);
        }
        catch (SocketException)
        {
            // peer port not bound yet; presence resumes once it is
        }
    }

    private bool TryReceive(out byte[] data)
    {
        data = [];
        while (_socket.Available > 0)
        {
            var from = new IPEndPoint(IPAddress.Any, 0);
            try
            {
                var bytes = _socket.Receive(ref from);
                if (from.Port != (int)PeerId) continue;
                data = bytes;
                return true;
            }
            catch (SocketException)
            {
                return false;
            }
        }
        return false;
    }
}
