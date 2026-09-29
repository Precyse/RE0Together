using System.Collections.Concurrent;
using System.Net;
using System.Net.Sockets;

namespace CoopLauncher;

/// <summary>Loopback TCP link to the in-game adapter: handshake, heartbeat and game frame relay.</summary>
public sealed class LoopbackBridge : IDisposable
{
    private const int HeartbeatIntervalMs = 1000;
    private const int LinkTimeoutMs = 5000;
    private const int SendTimeoutMs = 1000;

    private readonly GameProfile _profile;
    private readonly TcpListener _listener;
    private readonly ConcurrentQueue<(Connection Conn, Frame? Frame)> _inbox = new();
    private Connection? _current;
    private long _lastHeartbeatSent;

    public LoopbackBridge(GameProfile profile, int port)
    {
        _profile = profile;
        _listener = new TcpListener(IPAddress.Loopback, port);
        _listener.Start();
        Log.Info($"Loopback bridge listening on 127.0.0.1:{port}");
    }

    /// <summary>An adapter connection passed HELLO validation (also after a reconnect).</summary>
    public event Action? AdapterReady;

    /// <summary>Game frame (type >= 0x0100) from the adapter, opaque.</summary>
    public event Action<Frame>? GameFrame;

    public bool IsReady => _current is { Handshaken: true };

    public void Send(Frame frame)
    {
        if (_current is { Handshaken: true } conn) conn.Write(Framing.EncodeLoopback(frame), Drop);
    }

    public void Pump()
    {
        AcceptPending();
        while (_inbox.TryDequeue(out var item))
        {
            if (item.Conn != _current) continue;
            if (item.Frame is { } frame) Handle(item.Conn, frame);
            else Drop(item.Conn, "adapter disconnected");
        }
        if (_current == null) return;

        var now = Environment.TickCount64;
        if (now - _current.LastRx > LinkTimeoutMs) Drop(_current, "heartbeat timeout");
        else if (now - _lastHeartbeatSent >= HeartbeatIntervalMs)
        {
            _lastHeartbeatSent = now;
            _current.Write(Framing.EncodeLoopback(ControlMessages.Heartbeat()), Drop);
        }
    }

    public void Dispose()
    {
        _current?.Close();
        _listener.Stop();
    }

    private void AcceptPending()
    {
        while (_listener.Pending())
        {
            var client = _listener.AcceptTcpClient();
            _current?.Close();
            _current = new Connection(client, _inbox);
            Log.Info("Adapter connected");
        }
    }

    private void Handle(Connection conn, Frame frame)
    {
        conn.LastRx = Environment.TickCount64;
        if (frame.Type == Msg.Hello && !conn.Handshaken) ValidateHello(conn, frame);
        else if (frame.Type >= Msg.FirstGameType && conn.Handshaken) GameFrame?.Invoke(frame);
    }

    private void ValidateHello(Connection conn, Frame hello)
    {
        if (!ControlMessages.TryParseHello(hello.Payload, out var proto, out var gameId))
            Reject(conn, "bad hello");
        else if (proto != Framing.ProtocolVersion)
            Reject(conn, $"proto mismatch (launcher {Framing.ProtocolVersion}, adapter {proto})");
        else if (gameId != _profile.Id)
            Reject(conn, $"game mismatch (launcher {_profile.Id}, adapter {gameId})");
        else
        {
            conn.Handshaken = true;
            _lastHeartbeatSent = Environment.TickCount64;
            conn.Write(Framing.EncodeLoopback(ControlMessages.Heartbeat()), Drop);
            AdapterReady?.Invoke();
        }
    }

    private void Reject(Connection conn, string reason)
    {
        Log.Info($"Rejecting adapter: {reason}");
        conn.Write(Framing.EncodeLoopback(ControlMessages.Reject(reason)), Drop);
        conn.Close();
        if (conn == _current) _current = null;
    }

    private void Drop(Connection conn, string reason)
    {
        Log.Info($"Adapter link down: {reason}");
        conn.Close();
        if (conn == _current) _current = null;
    }

    private sealed class Connection
    {
        private readonly TcpClient _client;
        private readonly NetworkStream _stream;

        public Connection(TcpClient client, ConcurrentQueue<(Connection, Frame?)> inbox)
        {
            _client = client;
            _client.NoDelay = true;
            _client.SendTimeout = SendTimeoutMs;
            _stream = client.GetStream();
            LastRx = Environment.TickCount64;
            new Thread(() => ReadLoop(inbox)) { IsBackground = true, Name = "adapter-reader" }.Start();
        }

        public bool Handshaken { get; set; }

        public long LastRx { get; set; }

        public void Write(byte[] bytes, Action<Connection, string> onFail)
        {
            try
            {
                _stream.Write(bytes);
            }
            catch (Exception e) when (e is IOException or ObjectDisposedException)
            {
                onFail(this, "write failed");
            }
        }

        public void Close() => _client.Close();

        private void ReadLoop(ConcurrentQueue<(Connection, Frame?)> inbox)
        {
            while (Framing.TryReadLoopback(_stream, out var frame)) inbox.Enqueue((this, frame));
            inbox.Enqueue((this, null));
        }
    }
}
