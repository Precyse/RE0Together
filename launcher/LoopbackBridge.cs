using System.Collections.Concurrent;
using System.Net;
using System.Net.Sockets;

namespace CoopLauncher;

/// <summary>
/// Loopback TCP link to the in-game adapter: handshake, heartbeat and game frame relay. The loop thread never waits for the
/// game: every connection has an outgoing queue drained by its own writer thread. A game that stops reading (loading, a breakpoint)
/// first loses unreliable frames, and its link drops when the queue backs up past <see cref="MaxQueuedBytes"/>. A new local
/// connection (a port scanner, a second game copy) replaces the linked adapter only once its HELLO is valid.
/// </summary>
public sealed class LoopbackBridge : IDisposable
{
    /// <summary>Queued bytes above which unreliable frames are dropped instead of queued.</summary>
    public const int UnreliableDropBytes = 256 * 1024;

    /// <summary>Queued bytes above which the link is dropped.</summary>
    public const int MaxQueuedBytes = 4 * 1024 * 1024;

    private const int HeartbeatIntervalMs = 1000;
    private const int DefaultLinkTimeoutMs = 5000;
    private const long DropReportIntervalMs = 5000;

    private readonly GameProfile _profile;
    private readonly TcpListener _listener;
    private readonly int _linkTimeoutMs;
    private readonly ConcurrentQueue<(Connection Conn, Frame? Frame)> _inbox = new();
    private readonly List<Connection> _pending = [];
    private Connection? _current;
    private long _lastHeartbeatSent;
    private int _droppedSinceReport;
    private long _lastDropReportMs = Environment.TickCount64;

    /// <param name="linkTimeoutMs">How long a link may stay silent, and a connection may stay without a valid HELLO, before it is closed.</param>
    public LoopbackBridge(GameProfile profile, int port, int linkTimeoutMs = DefaultLinkTimeoutMs)
    {
        _profile = profile;
        _linkTimeoutMs = linkTimeoutMs;
        _listener = new TcpListener(IPAddress.Loopback, port);
        _listener.Start();
        Log.Info($"Loopback bridge listening on 127.0.0.1:{Port}");
    }

    /// <summary>An adapter connection passed HELLO validation (also after a reconnect).</summary>
    public event Action? AdapterReady;

    /// <summary>Game frame (type >= 0x0100) from the adapter, opaque.</summary>
    public event Action<Frame>? GameFrame;

    /// <summary>The adapter reports that the game wrote a save file (SAVE_CHANGED).</summary>
    public event Action? SaveChanged;

    /// <summary>The adapter reports a change the player made on the game's own co-op page (OPTION_SET), with the frame's payload.</summary>
    public event Action<byte[]>? OptionSet;

    /// <summary>The port the bridge listens on (the chosen one when the constructor was given 0).</summary>
    public int Port => ((IPEndPoint)_listener.LocalEndpoint).Port;

    public bool IsReady => _current != null;

    /// <summary>Unreliable frames dropped so far because the adapter was not reading fast enough.</summary>
    public int DroppedUnreliable { get; private set; }

    /// <summary>Queues the frame for the adapter; never blocks.</summary>
    public void Send(Frame frame)
    {
        if (_current is not { } conn) return;
        var bytes = Framing.EncodeLoopback(frame);
        var queued = conn.QueuedBytes;
        if ((frame.Flags & Framing.FlagReliable) == 0)
        {
            if (queued <= UnreliableDropBytes) conn.Enqueue(bytes);
            else
            {
                DroppedUnreliable++;
                _droppedSinceReport++;
            }
        }
        else if (queued + bytes.Length > MaxQueuedBytes) Drop(conn, "send queue full");
        else conn.Enqueue(bytes);
    }

    public void Pump()
    {
        AcceptPending();
        while (_inbox.TryDequeue(out var item)) Dispatch(item.Conn, item.Frame);
        var now = Environment.TickCount64;
        ClosePendingWithoutHello(now);
        ReportDrops(now);
        if (_current == null) return;

        if (now - _current.LastRx > _linkTimeoutMs) Drop(_current, "heartbeat timeout");
        else if (now - _lastHeartbeatSent >= HeartbeatIntervalMs)
        {
            _lastHeartbeatSent = now;
            _current.Enqueue(Framing.EncodeLoopback(ControlMessages.Heartbeat()));
        }
    }

    public void Dispose()
    {
        _current?.Close();
        foreach (var pending in _pending) pending.Close();
        _listener.Stop();
    }

    private void AcceptPending()
    {
        while (_listener.Pending()) _pending.Add(new Connection(_listener.AcceptTcpClient(), _inbox));
    }

    private void Dispatch(Connection conn, Frame? frame)
    {
        if (conn == _current)
        {
            if (frame is { } linked) Handle(conn, linked);
            else Drop(conn, "adapter disconnected");
        }
        else if (_pending.Contains(conn))
        {
            if (frame is not { } first) ClosePending(conn);
            else if (first.Type == Msg.Hello) ValidateHello(conn, first);
        }
    }

    private void Handle(Connection conn, Frame frame)
    {
        conn.LastRx = Environment.TickCount64;
        if (frame.Type >= Msg.FirstGameType) GameFrame?.Invoke(frame);
        else if (frame.Type == Msg.SaveChanged) SaveChanged?.Invoke();
        else if (frame.Type == Msg.OptionSet) OptionSet?.Invoke(frame.Payload);
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
            Link(conn);
    }

    /// <summary>The connection passed HELLO: it becomes the link, and an older link is closed.</summary>
    private void Link(Connection conn)
    {
        _pending.Remove(conn);
        _current?.Close();
        _current = conn;
        conn.LastRx = Environment.TickCount64;
        _lastHeartbeatSent = conn.LastRx;
        conn.Enqueue(Framing.EncodeLoopback(ControlMessages.Heartbeat()));
        Log.Info("Adapter connected");
        AdapterReady?.Invoke();
    }

    private void Reject(Connection conn, string reason)
    {
        Log.Info($"Rejecting adapter: {reason}");
        conn.Enqueue(Framing.EncodeLoopback(ControlMessages.Reject(reason)));
        _pending.Remove(conn);
        conn.CloseAfterFlush();
    }

    private void ClosePending(Connection conn)
    {
        _pending.Remove(conn);
        conn.Close();
    }

    private void ClosePendingWithoutHello(long now)
    {
        foreach (var conn in _pending.Where(c => now - c.CreatedMs > _linkTimeoutMs).ToList())
        {
            Log.Info("Closed an adapter connection that sent no HELLO");
            ClosePending(conn);
        }
    }

    private void ReportDrops(long now)
    {
        if (now - _lastDropReportMs < DropReportIntervalMs) return;
        _lastDropReportMs = now;
        if (_droppedSinceReport == 0) return;
        Log.Info($"Adapter send queue: dropped {_droppedSinceReport} unreliable frames, the game is not reading them fast enough");
        _droppedSinceReport = 0;
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
        private readonly Queue<byte[]> _outgoing = new();
        private long _queuedBytes;
        private bool _closed;
        private bool _closeAfterFlush;

        public Connection(TcpClient client, ConcurrentQueue<(Connection, Frame?)> inbox)
        {
            _client = client;
            _client.NoDelay = true;
            _client.SendTimeout = DefaultLinkTimeoutMs;
            _stream = client.GetStream();
            CreatedMs = Environment.TickCount64;
            LastRx = CreatedMs;
            new Thread(() => ReadLoop(inbox)) { IsBackground = true, Name = "adapter-reader" }.Start();
            new Thread(() => WriteLoop(inbox)) { IsBackground = true, Name = "adapter-writer" }.Start();
        }

        public long CreatedMs { get; }

        public long LastRx { get; set; }

        /// <summary>Bytes queued or being written; the loop thread reads it to decide what to drop.</summary>
        public long QueuedBytes => Interlocked.Read(ref _queuedBytes);

        public void Enqueue(byte[] bytes)
        {
            lock (_outgoing)
            {
                if (_closed || _closeAfterFlush) return;
                _outgoing.Enqueue(bytes);
                Interlocked.Add(ref _queuedBytes, bytes.Length);
                Monitor.Pulse(_outgoing);
            }
        }

        public void Close()
        {
            lock (_outgoing)
            {
                _closed = true;
                Monitor.PulseAll(_outgoing);
            }
            _client.Close();
        }

        /// <summary>Closes once everything queued has been written (a REJECT must reach the adapter).</summary>
        public void CloseAfterFlush()
        {
            lock (_outgoing)
            {
                _closeAfterFlush = true;
                Monitor.PulseAll(_outgoing);
            }
        }

        private void ReadLoop(ConcurrentQueue<(Connection, Frame?)> inbox)
        {
            while (Framing.TryReadLoopback(_stream, out var frame)) inbox.Enqueue((this, frame));
            inbox.Enqueue((this, null));
        }

        /// <summary>Writes the queue in order. A write that fails or times out ends the link: the loop hears of it as a disconnect.</summary>
        private void WriteLoop(ConcurrentQueue<(Connection, Frame?)> inbox)
        {
            while (true)
            {
                byte[] bytes;
                lock (_outgoing)
                {
                    while (_outgoing.Count == 0 && !_closed && !_closeAfterFlush) Monitor.Wait(_outgoing);
                    if (_closed) return;
                    if (_outgoing.Count == 0)
                    {
                        _client.Close();
                        return;
                    }
                    bytes = _outgoing.Dequeue();
                }
                try
                {
                    _stream.Write(bytes);
                }
                catch (Exception e) when (e is IOException or ObjectDisposedException)
                {
                    inbox.Enqueue((this, null));
                    return;
                }
                Interlocked.Add(ref _queuedBytes, -bytes.Length);
            }
        }
    }
}
