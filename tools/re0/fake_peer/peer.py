"""The fake machine: what a second adapter would hold (identity, place, clock, sequence counters), the periodic status it
sends, and a view of what the other side (the game) last said. Every emulator reads and writes this one object."""
import time
from collections import Counter

from fake_peer import wire
from fake_peer.link import Module
from fake_peer.wire import Msg

PLAYER_STATE_HZ = 20.0
ROOM_STATE_PERIOD_S = 2.0
HOST_STATUS_PERIOD_S = 1.0
DEFAULT_HP = 150  # partner_status::kBaseMaxHp: full health, so the game shows the fake as fine
POSE_FRESH_MS = 2000.0
THINK_BUSY_GROUP = 2


def mono_ms():
    return time.monotonic() * 1000.0


class Place:
    """Where the fake's character is: its room, the entry it came by, its load serial and a running door."""

    def __init__(self, scene=wire.NO_SCENE, entry=0, mode=wire.AUTO_SPOT):
        self.scene, self.entry, self.mode = scene, entry, mode
        self.load_serial = 1
        self.door_target = wire.NO_SCENE
        self.door_flags = 0


class Peer(Module):
    """Identity, place, clock and counters of the fake, and the status messages every machine repeats."""

    def __init__(self, ctx, character, status=True):
        super().__init__(ctx)
        self.character = character
        self.status = status
        self.place = Place()
        self.pos = (0.0, 0.0, 0.0)
        self.quat = wire.IDENTITY_QUAT
        self.hp = DEFAULT_HP
        self.party_mode = 0
        self.think_override = None  # (group, state, phase) every pose reports instead of its own
        self.save_phase = 1  # the host's announced room phase: 1 Main
        self.counters = Counter()
        self._next_player_state = 0.0
        self._next_room_state = 0.0
        self._next_host_status = 0.0

    def next(self, name):
        """The next number of a sequence counter (POSE seq, MOTION seq, ROOM_MOVE seq, batches, ...)."""
        self.counters[name] += 1
        return self.counters[name]

    def next_u16(self, name):
        """Like `next` for a 16 bit counter that is never 0."""
        self.counters[name] = self.counters[name] % 0xFFFF + 1
        return self.counters[name]

    def sender_ms(self):
        return int(mono_ms()) & 0xFFFFFFFF

    @property
    def is_host(self):
        return self.ctx.link.session.is_host

    @property
    def slot(self):
        return self.ctx.link.session.fake_slot

    @property
    def game_character(self):
        return wire.REBECCA + wire.BILLY - self.character

    def place_at(self, scene, entry=0, mode=wire.AUTO_SPOT):
        """The fake's character is now in `scene` (a new load serial); status goes out at once."""
        p = self.place
        p.scene, p.entry, p.mode = scene, entry, mode
        p.load_serial = self.next_u16("load_serial")
        p.door_target, p.door_flags = wire.NO_SCENE, 0
        self.send_room_state()

    def tick(self, now):
        if not (self.status and self.ctx.link.connected.is_set() and self.ctx.link.session.peer_up.is_set()):
            return
        if now >= self._next_player_state:
            self._next_player_state = now + 1.0 / PLAYER_STATE_HZ
            self.send_player_state()
        if now >= self._next_room_state:
            self.send_room_state()
        if self.is_host and now >= self._next_host_status:
            self._next_host_status = now + HOST_STATUS_PERIOD_S
            self.send_host_status()

    def send_player_state(self):
        self.ctx.link.send(Msg.PLAYER_STATE, wire.PlayerState.pack(
            seq=self.next("player_state"), pos=self.pos, quat=self.quat, character=self.character,
            sender_is_host=int(self.is_host), room=self.place.scene, hp=self.hp, focused=wire.NO_TARGET),
            flags=0)

    def send_room_state(self):
        self._next_room_state = time.monotonic() + ROOM_STATE_PERIOD_S
        p = self.place
        self.ctx.link.send(Msg.ROOM_STATE, wire.RoomState.pack(
            scene=p.scene, partner_in_room=int(self.ctx.remote.in_room(p.scene)), door_target=p.door_target,
            door_flags=p.door_flags))

    def send_host_status(self):
        owners = [0, 0]
        owners[self.character] = self.slot
        owners[self.game_character] = self.ctx.link.session.game_slot
        self.ctx.link.send(Msg.OWNERSHIP, wire.Ownership.pack(billy_owner=owners[wire.BILLY], rebecca_owner=owners[wire.REBECCA]))
        self.ctx.link.send(Msg.PARTY_MODE, bytes((self.party_mode,)))
        self.ctx.link.send(Msg.SAVE_SLOT, wire.SaveSlot.pack(slot=0, phase=self.save_phase))


class RemoteView(Module):
    """What the game side last said: its pose (room, load serial, position, think), PLAYER_STATE, room moves, party mode."""

    HANDLES = {Msg.POSE_STATE: "on_pose", Msg.PLAYER_STATE: "on_player_state", Msg.ROOM_MOVE: "on_room_move",
               Msg.ROOM_STATE: "on_room_state", Msg.PARTY_MODE: "on_party_mode", Msg.PLAYER_DIED: "on_died"}

    def __init__(self, ctx):
        super().__init__(ctx)
        self.pose = None
        self.pose_arrival = 0.0
        self.character = None
        self.hp = None
        self.room_state = None
        self.move = None
        self.party_mode = None
        self.died = []
        self.poses_seen = 0

    def on_pose(self, frame):
        self.pose = wire.PoseState.unpack(frame.payload)
        self.pose_arrival = time.monotonic()
        self.poses_seen += 1

    def on_player_state(self, frame):
        state = wire.PlayerState.unpack(frame.payload)
        self.character, self.hp = state.character, state.hp

    def on_room_state(self, frame):
        self.room_state = wire.RoomState.unpack(frame.payload)

    def on_room_move(self, frame):
        self.move = wire.RoomMove.unpack(frame.payload)

    def on_party_mode(self, frame):
        self.party_mode = frame.payload[0]

    def on_died(self, frame):
        self.died.append(frame.payload[0])

    def pose_age_ms(self):
        return (time.monotonic() - self.pose_arrival) * 1000.0 if self.pose else float("inf")

    def pose_fresh(self):
        return self.pose_age_ms() <= POSE_FRESH_MS

    def in_room(self, scene):
        return self.pose is not None and self.pose.room == scene and self.pose_fresh()

    def position(self):
        return tuple(self.pose.pos) if self.pose else (0.0, 0.0, 0.0)
