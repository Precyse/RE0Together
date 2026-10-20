"""Session flows: SNAPSHOT_REQUEST / JOIN_SNAPSHOT / FLOOR_SNAPSHOT / JOIN_EXTRAS / SESSION_READY / RESYNC_REQUEST.

As the guest (the game is host) the fake asks for a snapshot, feeds its mirrors from the answers and sends SESSION_READY
until the host echoes it. As the host (the game is guest) it answers a snapshot request with the join snapshot (built from
the running game through fake_session, read-only, or zeros with --no-game), the floor items and JOIN_EXTRAS, and judges the
guest's SESSION_READY the way session_ready_rule::judge does."""
import struct

from fake_peer import wire
from fake_peer.link import Module
from fake_peer.wire import Msg

REQUEST_PERIOD_S = 2.0
READY_PERIOD_S = 1.0
POSE_FRESH_MS = 2000.0
SNAPSHOT_SCENE_OFFSET = 2  # CharacterPlace: u8 hasDoor, u8 reserved, u16 scene
ITEMS, BILLY_ITEMS, REBECCA_ITEMS, ITEM_BLOCK = 0xDCBF44, 0x64, 0x24, 0x40  # fake_session's reads of the game
FLAG_MANAGER, FLAG_BITS, FLAG_BYTES = 0xDCC014, 0x20, 0x11C
UNKNOWN_SNAPSHOT_STEP = 1000


def judge_ready(ready, pose, pose_age_ms, latest_serial):
    """session_ready_rule::judge: None when accepted, else the refusal reason."""
    if ready.snapshot_serial > latest_serial:
        return "unknown snapshot"
    if pose is None:
        return "no pose"
    if pose_age_ms > POSE_FRESH_MS:
        return "stale pose"
    if pose.room != ready.scene:
        return "other room"
    if pose.load_serial != ready.load_serial:
        return "other load"
    return None


class SessionFlows(Module):
    HANDLES = {Msg.SNAPSHOT_REQUEST: "on_request", Msg.JOIN_SNAPSHOT: "on_snapshot", Msg.FLOOR_SNAPSHOT: "on_floor",
               Msg.JOIN_EXTRAS: "on_extras", Msg.SESSION_READY: "on_ready", Msg.RESYNC_REQUEST: "on_resync_request"}

    def __init__(self, ctx):
        super().__init__(ctx)
        self.no_echo = False
        self.snapshots = 0
        self.floor = []  # FloorEntry records received (guest) or to send (host: dicts)
        self.extras = None  # the last JOIN_EXTRAS received (guest)
        self.latest_serial = 0  # host: the serial of the newest JOIN_EXTRAS sent
        self.refusals = []  # (reason, scene, serial) the host judged
        self.echoes = []  # SessionReady records echoed (host) or received (guest)
        self.readies = []  # (scene, load serial, snapshot serial) of every ready the fake sent
        self.echoes_before_bad = 0
        self.requests = 0  # SNAPSHOT_REQUESTs seen (host) or sent (guest)
        self._awaiting = False
        self._ready_armed = False
        self._next_request = 0.0
        self._next_ready = 0.0
        self._snapshot_serial = 0

    # ---- guest ---------------------------------------------------------------------------------------------------

    def request_snapshot(self):
        """Asks the host for a snapshot and repeats the request every 2 s until one arrives."""
        self._awaiting, self._ready_armed = True, False
        self._next_request = 0.0

    def _send_request(self):
        self.requests += 1
        self.ctx.link.send(Msg.SNAPSHOT_REQUEST, struct.pack("<H", self.ctx.peer.place.scene))

    def on_snapshot(self, frame):
        if len(frame.payload) != wire.JOIN_SNAPSHOT_SIZE:
            raise wire.WireError(f"JOIN_SNAPSHOT: {len(frame.payload)} bytes")
        self._awaiting = False
        self.snapshots += 1
        self.ctx.flags.adopt_words(frame.payload)
        peer = self.ctx.peer
        at = SNAPSHOT_SCENE_OFFSET + wire.PLACE_SIZE * peer.character
        scene = struct.unpack_from("<H", frame.payload, at)[0]
        if scene < wire.SCENE_LIMIT:
            self.ctx.rooms.known.add(scene)
            self.ctx.rooms.move(scene)

    def on_floor(self, frame):
        self.floor = wire.unpack_floor_snapshot(frame.payload)[1]
        self.ctx.items.learn_floor(self.floor)

    def on_extras(self, frame):
        header, claims, props, items = wire.unpack_extras(frame.payload)
        self.extras = header
        self.ctx.authority.apply_extras(claims, self.ctx.link.session.game_slot)
        self.ctx.flags.adopt_serial(header.flag_serial)
        self.ctx.props.adopt(props)
        self.ctx.items.adopt_consumed(items)
        self._snapshot_serial = header.snapshot_serial
        self._ready_armed = True
        self._next_ready = 0.0

    def send_ready(self, scene=None, load_serial=None, snapshot_serial=None):
        """One SESSION_READY of the fake's current place (or the given values, for the negative cases)."""
        peer = self.ctx.peer
        ready = wire.SessionReady.make(
            phase=wire.READY, scene=peer.place.scene if scene is None else scene,
            load_serial=peer.place.load_serial if load_serial is None else load_serial,
            snapshot_serial=self._snapshot_serial if snapshot_serial is None else snapshot_serial)
        self.readies.append((ready.scene, ready.load_serial, ready.snapshot_serial))
        self.ctx.link.send(Msg.SESSION_READY, wire.SessionReady.pack(**ready._asdict()))

    def mark_bad_start(self):
        """From here every ready the fake sends is one the host must refuse; echoes counted so far are the good ones."""
        self.echoes_before_bad = len(self.echoes)

    def send_bad_ready(self, kind):
        """A SESSION_READY the host must refuse: a snapshot never issued, another room, another load."""
        peer = self.ctx.peer
        if kind == "unknown_snapshot":
            self.send_ready(snapshot_serial=self._snapshot_serial + UNKNOWN_SNAPSHOT_STEP)
        elif kind == "other_room":
            other = next((s for s in sorted(self.ctx.rooms.known) if s != peer.place.scene), None)
            if other is None:
                raise ValueError("no second room the game has named")
            self.send_ready(scene=other)
        elif kind == "other_load":
            self.send_ready(load_serial=peer.place.load_serial % 0xFFFF + 1)
        else:
            raise ValueError(f"unknown bad ready {kind}")

    # ---- host ----------------------------------------------------------------------------------------------------

    def on_request(self, frame):
        self.requests += 1
        peer, ctx = self.ctx.peer, self.ctx
        snapshot = self.snapshot_bytes()
        ctx.link.send(Msg.JOIN_SNAPSHOT, snapshot)
        ctx.link.send(Msg.FLOOR_SNAPSHOT, wire.pack_floor_snapshot(ctx.items.floor_for_snapshot()))
        self._snapshot_serial = peer.next("snapshot_serial")
        self.latest_serial = self._snapshot_serial
        claims = [dict(id=m.id, epoch=m.epoch, owner=m.owner) for m in ctx.authority.claims_of(peer.slot)]
        ctx.link.send(Msg.JOIN_EXTRAS, wire.pack_extras(self._snapshot_serial, ctx.flags.serial, claims,
                                                       ctx.props.commit_dicts(), ctx.items.consumed_dicts()))

    def snapshot_bytes(self):
        """The JOIN_SNAPSHOT payload: both characters' places, both inventories and the flags. The places are the fake's and the
        game's last known; with a game running the inventories and flags come from its memory through fake_session (read only),
        with --no-game they are zeros."""
        import fake_session
        peer, remote = self.ctx.peer, self.ctx.remote
        own = fake_session.place(peer.place.scene, peer.place.entry, peer.character, peer.pos)
        game_scene = remote.pose.room if remote.pose else peer.place.scene
        theirs = fake_session.place(game_scene, 0, peer.game_character, remote.position())
        places = b"".join((own, theirs) if peer.character == wire.BILLY else (theirs, own))
        if self.ctx.options.no_game:
            return places + bytes(wire.JOIN_SNAPSHOT_SIZE - len(places))
        return (places + fake_session.game_bytes(ITEMS, BILLY_ITEMS, ITEM_BLOCK) +
                fake_session.game_bytes(ITEMS, REBECCA_ITEMS, ITEM_BLOCK) + fake_session.game_bytes(FLAG_MANAGER, FLAG_BITS, FLAG_BYTES))

    def on_ready(self, frame):
        ready = wire.SessionReady.unpack(frame.payload)
        if ready.phase == wire.ECHO:
            self.echoes.append(ready)
            return
        remote = self.ctx.remote
        reason = judge_ready(ready, remote.pose, remote.pose_age_ms(), self.latest_serial)
        if reason:
            self.refusals.append((reason, ready.scene, ready.snapshot_serial))
            return
        if not self.no_echo:
            self.ctx.link.send(Msg.SESSION_READY, wire.SessionReady.pack(
                phase=wire.ECHO, scene=ready.scene, load_serial=ready.load_serial, snapshot_serial=ready.snapshot_serial))
            self.echoes.append(ready._replace(phase=wire.ECHO))

    def send_resync_request(self):
        """The host asks the guest to take a new snapshot."""
        self.ctx.link.send(Msg.RESYNC_REQUEST, b"")

    def on_resync_request(self, frame):
        self.request_snapshot()

    # ---- periodic ------------------------------------------------------------------------------------------------

    def tick(self, now):
        if self._awaiting and now >= self._next_request:
            self._next_request = now + REQUEST_PERIOD_S
            self._send_request()
        if self._ready_armed and now >= self._next_ready:
            self._next_ready = now + READY_PERIOD_S
            if self.echoes and self._echo_matches():
                self._ready_armed = False
            else:
                self.send_ready()

    def _echo_matches(self):
        peer = self.ctx.peer
        last = self.echoes[-1]
        return (last.scene, last.load_serial, last.snapshot_serial) == (peer.place.scene, peer.place.load_serial, self._snapshot_serial)
