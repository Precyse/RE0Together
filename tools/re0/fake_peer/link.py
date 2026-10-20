"""Transports to the game's side of the session, heartbeats, the link shaper and the receive dispatch.

`BridgeLink` connects to a launcher's loopback bridge as a second adapter (real launchers, or a hand-started one).
`StandinLink` listens as the game's own launcher (the game is the guest; no launcher runs): WELCOME, PEER_UP, heartbeats,
PEER_DOWN and a new epoch on request. Both give the same `Link` surface: `send`, `session`, `on_frame`.
"""
import heapq
import random
import socket
import threading
import time
from collections import Counter, defaultdict

from fake_peer import wire
from fake_peer.library import IN, OUT
from fake_peer.wire import Frame, Msg

HEARTBEAT_S = 1.0
IDLE_POLL_S = 0.002
CONNECT_RETRY_S = 0.25
CONNECT_TIMEOUT_S = 30.0
ACCEPT_TIMEOUT_S = 0.5
MAX_PLAYERS = 2
FAKE_STEAM_ID = 76561190000000001
FAKE_NAME = "fake peer"
GAME_ID = "re0"
GAME_PORT = 27960


class ShaperConfig:
    def __init__(self, delay_ms=0.0, jitter_ms=0.0, loss=0.0):
        self.delay_ms, self.jitter_ms, self.loss = delay_ms, jitter_ms, loss


class Shaper:
    """Delay, jitter and loss for the frames the fake sends, plus `cut` and `stall` windows (a Steam link that stalls)."""

    def __init__(self, config=None, rng=None, clock=time.monotonic):
        self.config = config or ShaperConfig()
        self._rng = rng or random.Random()
        self._clock = clock
        self._cut_until = 0.0
        self._stall_until = 0.0
        self._last_reliable_due = 0.0

    def cut(self, seconds):
        """Unreliable frames dropped, reliable ones held, until the window ends; then the held ones go in order."""
        self._cut_until = self._clock() + seconds

    def stall(self, seconds):
        """Everything held until the window ends, then sent at once."""
        self._stall_until = self._clock() + seconds

    def idle(self):
        c = self.config
        now = self._clock()
        return not (c.delay_ms or c.jitter_ms or c.loss) and now >= self._cut_until and now >= self._stall_until

    def plan(self, reliable):
        """The monotonic time to send a frame, or None to drop it."""
        now = self._clock()
        if now < self._stall_until:
            return self._ordered(reliable, self._stall_until)
        if now < self._cut_until:
            return self._ordered(True, self._cut_until) if reliable else None
        if not reliable and self._rng.random() < self.config.loss:
            return None
        due = now + (self.config.delay_ms + self._rng.uniform(0.0, self.config.jitter_ms)) / 1000.0
        return self._ordered(reliable, due)

    def _ordered(self, reliable, due):
        if reliable:
            due = max(due, self._last_reliable_due)
            self._last_reliable_due = due
        return due


class SessionInfo:
    """What the launcher (or the stand-in) told this side: slots, host, epoch."""

    def __init__(self):
        self.fake_slot = None
        self.host_slot = wire.HOST_SLOT
        self.game_slot = None
        self.epoch = 0
        self.max_players = MAX_PLAYERS
        self.welcomed = threading.Event()
        self.peer_up = threading.Event()

    @property
    def is_host(self):
        return self.fake_slot is not None and self.fake_slot == self.host_slot

    @property
    def ready(self):
        return self.welcomed.is_set() and self.peer_up.is_set()


class Link:
    """Frame transport common to both stand-in and bridge: sending (shaped, journaled), heartbeats, receive loop."""

    def __init__(self, journal, on_frame, shaper=None):
        self.journal = journal
        self.on_frame = on_frame
        self.shaper = shaper or Shaper()
        self.session = SessionInfo()
        self.counts = Counter()
        self.rejected = None
        self._sock = None
        self._send_lock = threading.Lock()
        self._queue = []
        self._queue_lock = threading.Lock()
        self._order = 0
        self._stop = threading.Event()
        self._heartbeats_off_until = 0.0
        self._last_heartbeat = 0.0
        self.guard = None
        self.abort = None  # called while connecting: True gives up (the launcher died)
        self.connected = threading.Event()

    # ---- sending -------------------------------------------------------------------------------------------------

    def send(self, msg_type, payload=b"", flags=wire.FLAG_RELIABLE, slot=None):
        """Sends one game frame to the other side, through the shaper. Returns False when the link is down."""
        if slot is None:
            slot = self.session.game_slot if self.session.game_slot is not None else wire.SLOT_ALL
        frame = Frame(int(msg_type), flags, slot, bytes(payload))
        if self.guard and not self.guard(frame.type):
            self.counts["refused_send"] += 1
            self.journal.note(f"refused to send {wire.type_name(frame.type)}")
            return False
        if self.shaper.idle() and not self._queue:
            return self._write(frame)
        due = self.shaper.plan(bool(flags & wire.FLAG_RELIABLE))
        if due is None:
            self.counts["shaped_drop"] += 1
            self.journal.note(f"shaper dropped {wire.type_name(frame.type)}")
            return True
        with self._queue_lock:
            self._order += 1
            heapq.heappush(self._queue, (due, self._order, frame))
        return True

    def _write(self, frame, journal=True):
        data = wire.encode_frame(frame.type, frame.payload, frame.flags, frame.slot)
        sock = self._sock
        if sock is None:
            return False
        try:
            with self._send_lock:
                sock.sendall(data)
        except OSError:
            self._drop(sock)
            return False
        if journal:
            self.journal.frame(OUT, frame)
        self.counts["sent"] += 1
        return True

    def pump(self):
        """Sends the shaped frames that are due; called by the sender thread."""
        now = time.monotonic()
        while True:
            with self._queue_lock:
                if not self._queue or self._queue[0][0] > now:
                    return
                _, _, frame = heapq.heappop(self._queue)
            self._write(frame)

    def _sender_loop(self):
        while not self._stop.is_set():
            self.pump()
            now = time.monotonic()
            if now >= self._heartbeats_off_until and now - self._last_heartbeat >= HEARTBEAT_S:
                self._last_heartbeat = now
                self._write(Frame(Msg.HEARTBEAT, 0, wire.SLOT_ALL, b""), journal=False)
            time.sleep(IDLE_POLL_S)

    def stop_heartbeats(self, seconds):
        """The adapter's own 5 s link timeout, from the other side."""
        self._heartbeats_off_until = time.monotonic() + seconds

    # ---- receiving -----------------------------------------------------------------------------------------------

    def _read_loop(self, sock):
        try:
            while not self._stop.is_set():
                self._receive(wire.read_frame(sock))
        except (OSError, ConnectionError):
            self._drop(sock)

    def _receive(self, frame):
        if frame.type == Msg.WELCOME:
            welcome = wire.parse_welcome(frame.payload)
            self.session.fake_slot, self.session.host_slot = welcome["local_slot"], welcome["host_slot"]
            self.session.epoch, self.session.max_players = welcome["epoch"], welcome["max_players"]
            self.session.welcomed.set()
        elif frame.type == Msg.PEER_UP:
            self.session.game_slot = frame.payload[0]
            self.session.peer_up.set()
        elif frame.type == Msg.PEER_DOWN:
            self.session.peer_up.clear()
            self.session.game_slot = None
        elif frame.type == Msg.REJECT:
            self.rejected = frame.payload.decode("utf-8", "replace")
        elif frame.type >= wire.FIRST_GAME_TYPE:
            self._deliver(frame)
            return
        if frame.type != Msg.HEARTBEAT:
            self.journal.frame(IN, frame)

    def _deliver(self, frame):
        self.journal.frame(IN, frame)
        self.counts["received"] += 1
        self.on_frame(frame)

    def _drop(self, sock):
        if sock is self._sock:
            self._sock = None
            self.connected.clear()
            self.journal.note("link down")
        try:
            sock.close()
        except OSError:
            pass

    def _attach(self, sock):
        self._sock = sock
        self.connected.set()
        threading.Thread(target=self._read_loop, args=(sock,), daemon=True).start()

    def _start_sender(self):
        threading.Thread(target=self._sender_loop, daemon=True).start()

    def close(self):
        self._stop.set()
        sock, self._sock = self._sock, None
        if sock:
            try:
                sock.close()
            except OSError:
                pass


class BridgeLink(Link):
    """A second adapter on a launcher's loopback bridge."""

    def __init__(self, port, journal, on_frame, shaper=None):
        super().__init__(journal, on_frame, shaper)
        if port == GAME_PORT:
            raise ValueError(f"port {GAME_PORT} is the game's own link")
        self.port = port

    def start(self, timeout=CONNECT_TIMEOUT_S):
        deadline = time.monotonic() + timeout
        while True:
            try:
                sock = wire.connect("127.0.0.1", self.port)
                break
            except OSError:
                if time.monotonic() > deadline or (self.abort and self.abort()):
                    raise ConnectionError(f"no launcher bridge on 127.0.0.1:{self.port}") from None
                time.sleep(CONNECT_RETRY_S)
        self._attach(sock)
        self._write(Frame(Msg.HELLO, wire.FLAG_RELIABLE, 0, wire.hello_payload(GAME_ID)), journal=False)
        self._start_sender()


class StandinLink(Link):
    """The game's launcher for a game that is the guest (listens on the game's own port, only for that case)."""

    def __init__(self, port, journal, on_frame, shaper=None, fake_slot=wire.HOST_SLOT):
        super().__init__(journal, on_frame, shaper)
        self.port = port
        self.session.fake_slot = fake_slot
        self.session.host_slot = wire.HOST_SLOT
        self.session.game_slot = 1 - fake_slot
        self.session.epoch = 1
        self._server = None

    def start(self):
        self._server = socket.socket()
        self._server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._server.bind(("127.0.0.1", self.port))
        self._server.listen(1)
        self._server.settimeout(ACCEPT_TIMEOUT_S)
        threading.Thread(target=self._accept_loop, daemon=True).start()
        self._start_sender()

    def _accept_loop(self):
        while not self._stop.is_set():
            try:
                sock, _ = self._server.accept()
            except (socket.timeout, TimeoutError):
                continue
            except OSError:
                return
            sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            sock.settimeout(None)
            try:
                wire.read_frame(sock)  # HELLO
            except (OSError, ConnectionError):
                continue
            self._attach(sock)
            self._announce()

    def _announce(self):
        """WELCOME as the game would see it (its slot, the host, the epoch) and the fake as the peer that is up."""
        s = self.session
        self._write(Frame(Msg.WELCOME, wire.FLAG_RELIABLE, wire.HOST_SLOT,
                          wire.welcome_payload(s.game_slot, s.host_slot, s.max_players, s.epoch)), journal=False)
        self._write(Frame(Msg.PEER_UP, wire.FLAG_RELIABLE, wire.HOST_SLOT,
                          wire.peer_up_payload(s.fake_slot, FAKE_STEAM_ID, FAKE_NAME)), journal=False)
        s.welcomed.set()
        s.peer_up.set()

    def new_epoch(self):
        """A new WELCOME with the epoch one up (the game's launcher re-formed the session)."""
        self.session.epoch += 1
        self._announce()

    def peer_down(self):
        """The fake leaves: PEER_DOWN for its slot."""
        self._write(Frame(Msg.PEER_DOWN, wire.FLAG_RELIABLE, wire.HOST_SLOT, bytes((self.session.fake_slot,))))
        self.session.peer_up.clear()

    def peer_up(self):
        self._write(Frame(Msg.PEER_UP, wire.FLAG_RELIABLE, wire.HOST_SLOT,
                          wire.peer_up_payload(self.session.fake_slot, FAKE_STEAM_ID, FAKE_NAME)), journal=False)
        self.session.peer_up.set()

    def _receive(self, frame):
        if frame.type >= wire.FIRST_GAME_TYPE:
            frame = frame._replace(slot=self.session.game_slot)
        super()._receive(frame)

    def close(self):
        super().close()
        if self._server:
            self._server.close()


class Module:
    """A group of handlers and a periodic step. `HANDLES` maps a message type to the name of the method that takes it."""

    HANDLES = {}

    def __init__(self, ctx):
        self.ctx = ctx

    def tick(self, now):
        """Called every few milliseconds with the monotonic time; override for periodic work."""


class Dispatcher:
    """Hands each received game frame to the modules that handle its type; a malformed or faulty one is counted, not fatal."""

    def __init__(self, journal, lock):
        self._handlers = defaultdict(list)
        self._journal = journal
        self._lock = lock
        self.malformed = Counter()
        self.faults = Counter()

    def register(self, module):
        for msg_type, method in module.HANDLES.items():
            self._handlers[int(msg_type)].append(getattr(module, method))

    def dispatch(self, frame):
        with self._lock:
            for handler in self._handlers.get(frame.type, ()):
                try:
                    handler(frame)
                except wire.WireError as error:
                    self.malformed[frame.type] += 1
                    self._journal.note(f"malformed {wire.type_name(frame.type)}: {error}")
                except Exception as error:  # a handler fault must not end the run
                    self.faults[frame.type] += 1
                    self._journal.note(f"fault in {wire.type_name(frame.type)} handler: {type(error).__name__}: {error}")
