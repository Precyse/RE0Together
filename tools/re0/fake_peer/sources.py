"""Pose sources: where the fake character's body (POSE_STATE 0x0140, MOTION_EVENT 0x0141, WEAPON_FX 0x0142) comes from.

One source is active at a time: echo (the game's own streams re-sent later and beside it, "mirror myself"), replay (a
recorded clip), stand (one pose held at a point), held (the last pose resent with the held flag) or silent. Everything is
re-stamped with the fake's clock, sequences and place, so the game sees one consistent second machine.
"""
import heapq
import struct
import time

from fake_peer import wire
from fake_peer.link import Module
from fake_peer.wire import Msg

POSE_HZ = 30.0
HELD_HZ = 4.0
DEFAULT_ECHO_DELAY_S = 1.0
DEFAULT_ECHO_OFFSET_X = 150.0
STAND_OFFSET_X = 150.0
INPUT_ECHO_TYPES = (Msg.PLAYER_STATE, Msg.PAD_FRAME)
STATE_ECHO_TYPES = (Msg.POSE_STATE, Msg.MOTION_EVENT, Msg.WEAPON_FX)
PLAYER_STATE_POS_X = 4  # PLAYER_STATE is u32 seq, then pos[3]
MODES = ("echo", "replay", "stand", "held", "silent")
INPUT_TYPES = "input"


def shift_x(pos, dx):
    return (pos[0] + dx, pos[1], pos[2])


def shift_player_state_x(payload, dx):
    """A PLAYER_STATE payload with `dx` added to its x."""
    (x,) = struct.unpack_from("<f", payload, PLAYER_STATE_POS_X)
    return payload[:PLAYER_STATE_POS_X] + struct.pack("<f", x + dx) + payload[PLAYER_STATE_POS_X + 4:]


class Sources(Module):
    HANDLES = {Msg.POSE_STATE: "on_game_frame", Msg.MOTION_EVENT: "on_game_frame", Msg.WEAPON_FX: "on_game_frame",
               Msg.PLAYER_STATE: "on_game_frame", Msg.PAD_FRAME: "on_game_frame"}

    def __init__(self, ctx):
        super().__init__(ctx)
        self.mode = "silent"
        self.opts = {}
        self.last_pose = None  # the last PoseState record sent
        self.echoed = 0
        self._queue = []  # echo: heap of (due, order, type, payload)
        self._order = 0
        self._next_emit = 0.0
        self._clip_start = 0.0
        self._clip_index = 0

    def use(self, mode, **opts):
        """Switches the source. echo: delay, offset_x, types ("input" echoes PLAYER_STATE and PAD_FRAME untouched).
        replay: clip (a library item), loop, translate_to. stand: pos, motion. held and silent take nothing."""
        if mode not in MODES:
            raise ValueError(f"unknown pose source {mode}")
        self.mode, self.opts = mode, opts
        self._queue.clear()
        if mode == "replay":
            self._clip_start, self._clip_index = time.monotonic(), 0
        self.ctx.journal.note(f"pose source {mode}")

    # ---- echo ----------------------------------------------------------------------------------------------------

    def _input_echo(self):
        return self.opts.get("types") == INPUT_TYPES

    def on_game_frame(self, frame):
        wanted = INPUT_ECHO_TYPES if self._input_echo() else STATE_ECHO_TYPES
        if self.mode != "echo" or frame.type not in wanted:
            return
        self._order += 1
        heapq.heappush(self._queue, (time.monotonic() + self.opts.get("delay", DEFAULT_ECHO_DELAY_S), self._order,
                                     frame.type, frame.payload))

    def _echo_due(self, now):
        offset = self.opts.get("offset_x", 0.0 if self._input_echo() else DEFAULT_ECHO_OFFSET_X)
        while self._queue and self._queue[0][0] <= now:
            _, _, msg_type, payload = heapq.heappop(self._queue)
            if self._input_echo():
                self._send_input_echo(msg_type, payload, offset)
            else:
                self.emit(msg_type, payload, offset)
            self.echoed += 1

    def _send_input_echo(self, msg_type, payload, offset):
        if msg_type == Msg.PLAYER_STATE:
            self.ctx.link.send(msg_type, shift_player_state_x(payload, offset) if offset else payload, flags=0)
        else:
            self.ctx.link.send(msg_type, payload)

    # ---- re-stamping ---------------------------------------------------------------------------------------------

    def emit(self, msg_type, payload, dx=0.0, translate=None):
        """Sends a recorded or echoed body frame as the fake: own clock, sequence, place and character."""
        peer = self.ctx.peer
        if msg_type == Msg.POSE_STATE:
            pose = wire.PoseState.unpack(payload)
            if pose.room != peer.place.scene and self.mode == "echo":
                self.ctx.rooms.move(pose.room)
            pos = shift_x(pose.pos, dx) if translate is None else tuple(a + b for a, b in zip(pose.pos, translate))
            self.send_pose(pose._replace(pos=pos))
        elif msg_type == Msg.MOTION_EVENT:
            event = wire.MotionEvent.unpack(payload)._asdict()
            event.update(sender_ms=peer.sender_ms(), seq=peer.next("motion"))
            self.ctx.link.send(Msg.MOTION_EVENT, wire.MotionEvent.pack(**event))
        elif msg_type == Msg.WEAPON_FX:
            shifted = wire.shift_weapon_fx_x(payload, dx) if dx else payload
            count = wire.WeaponFxHeader.unpack(shifted[:wire.WeaponFxHeader.size]).count
            header = wire.WeaponFxHeader.pack(sender_ms=peer.sender_ms(), seq=peer.next("weapon_fx"), character=peer.character,
                                              count=count)
            self.ctx.link.send(Msg.WEAPON_FX, header + shifted[wire.WeaponFxHeader.size:])

    def send_pose(self, pose):
        """Sends a PoseState record with the fake's clock, sequence, room and load serial filled in."""
        peer = self.ctx.peer
        pose = pose._replace(sender_ms=peer.sender_ms(), seq=peer.next("pose"), room=peer.place.scene,
                             load_serial=peer.place.load_serial)
        if peer.think_override:
            pose = pose._replace(group=peer.think_override[0], state=peer.think_override[1], phase=peer.think_override[2])
        peer.pos, peer.quat = tuple(pose.pos), wire.unpack_quat(pose.quat)
        self.last_pose = pose
        self.ctx.link.send(Msg.POSE_STATE, wire.PoseState.pack(**pose._asdict()), flags=0)

    # ---- generated sources ---------------------------------------------------------------------------------------

    def _idle_motion(self):
        if "motion" in self.opts:
            return self.opts["motion"]
        clip = self.ctx.library.pose_clip(wire.CHARACTER_NAMES[self.ctx.peer.character])
        if clip:
            for item in clip["frames"]:
                if item["type"] == Msg.POSE_STATE:
                    return wire.PoseState.unpack(item["payload"]).motion0
        remote = self.ctx.remote.pose
        return remote.motion0 if remote else wire.NO_MOTION

    def _stand_pose(self):
        pos = self.opts.get("pos") or shift_x(self.ctx.remote.position(), STAND_OFFSET_X)
        return wire.PoseState.make(pos=pos, quat=wire.IDENTITY_PACKED_QUAT, motion0=self._idle_motion(),
                                   motion1=wire.NO_MOTION)

    def _play_clip(self, now):
        frames = self.opts["clip"]["frames"]
        elapsed_ms = (now - self._clip_start) * 1000.0
        translate = self._clip_translate(frames)
        while self._clip_index < len(frames) and frames[self._clip_index]["dt"] <= elapsed_ms:
            item = frames[self._clip_index]
            self.emit(item["type"], item["payload"], translate=translate)
            self._clip_index += 1
        if self._clip_index >= len(frames):
            if self.opts.get("loop"):
                self._clip_start, self._clip_index = now, 0
            else:
                self.mode = "held"

    def _clip_translate(self, frames):
        target = self.opts.get("translate_to")
        first = next((wire.PoseState.unpack(f["payload"]).pos for f in frames if f["type"] == Msg.POSE_STATE), None)
        if target is None or first is None:
            return (0.0, 0.0, 0.0)
        return tuple(t - f for t, f in zip(target, first))

    def tick(self, now):
        if not self.ctx.link.connected.is_set():
            return
        if self.mode == "echo":
            self._echo_due(now)
        elif self.mode == "replay":
            self._play_clip(now)
        elif self.mode == "stand" and now >= self._next_emit:
            self._next_emit = now + 1.0 / POSE_HZ
            self.send_pose(self._stand_pose())
        elif self.mode == "held" and self.last_pose and now >= self._next_emit:
            self._next_emit = now + 1.0 / HELD_HZ
            self.send_pose(self.last_pose._replace(flags=wire.POSE_FLAG_HELD))
