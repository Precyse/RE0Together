"""Enemies: the observer validation of the game's ENEMY_POSE / ENEMY_MOTION stream, the shooter (HIT_REQUEST built from a
captured one), the simulator by replay of a recorded enemy clip with its damage model, and the wrong-binding probes."""
import math
import time

from fake_peer import wire
from fake_peer import wire_check
from fake_peer.link import Module
from fake_peer.wire import Msg

POSE_HZ = 20.0
DEFAULT_HITS_TO_KILL = 3
CORPSE_PERIOD_S = 1.0
DEAD_HP = -1
RATE_TOLERANCE = 0.25
SPAWN_ID_STEP = 1
PROBES = ("binding", "epoch", "sender", "slot")
POOL_SLOTS = 140  # game.h kEnemyPoolSlots (7 class tables x 20)


class ObservedSlot:
    """What the game's stream said about one pool slot."""

    def __init__(self, entry, now):
        self.class_index, self.spawn_id, self.spawn_record, self.hp = entry.class_index, entry.spawn_id, entry.spawn_record, entry.hp
        self.first_t = self.last_t = now
        self.poses = 1
        self.terminals = 0
        self.removed = 0
        self.alive = bool(entry.flags & wire.ENEMY_ALIVE)
        self.last_alive_t = now if self.alive else None
        self.corpse_times = []


class ReplaySlot:
    """A recorded enemy under the damage model: its hit points, whether it still follows the recording, its death."""

    def __init__(self, entry, hits_to_kill):
        self.entry = entry
        self.start_hp = max(entry.hp, 1)
        self.step = max(self.start_hp // hits_to_kill, 1)
        self.hp = entry.hp
        self.detached = False  # hit at least once: holds its last recorded pose from now on
        self.terminal_sent = False
        self.corpse_at = 0.0
        self.hits = 0


class Enemies(Module):
    HANDLES = {Msg.ENEMY_POSE: "on_pose", Msg.ENEMY_MOTION: "on_motion", Msg.HIT_REQUEST: "on_hit_request",
               Msg.HIT_APPLIED: "on_hit_applied"}

    def __init__(self, ctx):
        super().__init__(ctx)
        self.observed = {}
        self.violations = []
        self.batches = 0
        self.motion_batches = 0
        self.last_seq = {}  # (message type, reliable) -> the newest batch number seen on that lane
        self.pose_times = []
        self.hit_requests = []  # HitRequest records the game sent (the fake simulates)
        self.hit_applied = []   # HitRequest records of HIT_APPLIED (the game simulates)
        self.vtables = wire_check.vtables("kEnemyVtables")
        self._replay = None
        self._replay_slots = {}
        self._paused = False
        self._scene = None
        self._index = 0
        self._start = 0.0
        self._corpse_next = 0.0
        self.hits_to_kill = DEFAULT_HITS_TO_KILL
        self._shot = None

    # ---- observer ------------------------------------------------------------------------------------------------

    def _violation(self, text):
        self.violations.append(text)
        self.ctx.journal.note(f"enemy stream: {text}")

    def _check_header(self, header, frame):
        authority = self.ctx.authority.table
        if authority.owner_of(header.scene) != frame.slot:
            self._violation(f"batch from slot {frame.slot} for scene 0x{header.scene:02x}: not the holder")
        if header.scene != self.ctx.peer.place.scene:
            self._violation(f"scene 0x{header.scene:02x} is not loaded here")
        if header.room_epoch != authority.epoch_of(header.scene):
            self._violation(f"epoch {header.room_epoch} is not the mirror's {authority.epoch_of(header.scene)}")
        lane = (frame.type, bool(frame.flags & wire.FLAG_RELIABLE))  # the launcher lets a reliable frame pass a held unreliable one
        if header.seq <= self.last_seq.get(lane, 0):
            self._violation(f"seq {header.seq} after {self.last_seq[lane]} on {lane}")
        self.last_seq[lane] = max(self.last_seq.get(lane, 0), header.seq)

    def on_pose(self, frame):
        header, entries = wire.unpack_enemy_batch(wire.EnemyPoseEntry, frame.payload)
        table = self.ctx.authority.table
        if table.owner_of(header.scene) == table.local:
            self.ctx.contact.on_holder_batch(header, entries, frame)
            return
        self._check_header(header, frame)
        now = time.monotonic()
        self.batches += 1
        self.pose_times.append(now)
        for entry in entries:
            self._observe(entry, now, frame.flags & wire.FLAG_RELIABLE)

    def _observe(self, entry, now, reliable):
        slot = self.observed.get(entry.slot)
        if slot is None or (slot.class_index, slot.spawn_id, slot.spawn_record) != (entry.class_index, entry.spawn_id, entry.spawn_record):
            self.observed[entry.slot] = slot = ObservedSlot(entry, now)
            return
        slot.poses += 1
        if entry.flags & wire.ENEMY_LEASED and not self.ctx.contact.lease_exists(entry.lease_token):
            self._violation(f"slot {entry.slot} leased pose with no lease")
        if entry.flags & wire.ENEMY_REMOVED:
            slot.removed += 1
        if entry.flags & wire.ENEMY_TERMINAL:
            slot.terminals += 1
            if not reliable:
                self._violation(f"slot {entry.slot} terminal sent unreliably")
        elif entry.flags & wire.ENEMY_ALIVE:
            if entry.hp > slot.hp:
                self._violation(f"slot {entry.slot} hp rose {slot.hp} to {entry.hp}")
            slot.alive, slot.last_alive_t = True, now
        else:
            slot.corpse_times.append(now)
            if len(slot.corpse_times) > 1 and now - slot.corpse_times[-2] < CORPSE_PERIOD_S * 0.5:
                self._violation(f"slot {entry.slot} corpse resent after {now - slot.corpse_times[-2]:.2f} s")
        slot.hp, slot.last_t = entry.hp, now

    def on_motion(self, frame):
        header, entries = wire.unpack_enemy_batch(wire.EnemyMotionEntry, frame.payload)
        self._check_header(header, frame)
        self.motion_batches += 1

    def on_hit_applied(self, frame):
        self.hit_applied.append(wire.HitRequest.unpack(frame.payload))

    def rate_hz(self):
        """Poses batches per second over the whole observed stream."""
        times = self.pose_times
        return (len(times) - 1) / (times[-1] - times[0]) if len(times) > 2 and times[-1] > times[0] else 0.0

    def rate_ok(self):
        return abs(self.rate_hz() - POSE_HZ) <= POSE_HZ * RATE_TOLERANCE

    def bound_slots(self):
        return sorted(self.observed)

    def arm_hp_check(self, slot):
        """Remembers a slot's HP before a shot, for `shot_hp_lower`."""
        self._shot = (slot, self.observed[slot].hp)

    def shot_hp_lower(self):
        """The stream's HP for the shot slot is below what it was when the check was armed."""
        return self._shot is not None and self.observed[self._shot[0]].hp < self._shot[1]

    # ---- shooter -------------------------------------------------------------------------------------------------

    def shoot(self, slot, attack_type=None):
        """HIT_REQUEST for an observed enemy, built from a captured one of its class (slot, room, vtable rewritten)."""
        target = self.observed.get(slot)
        if target is None:
            return False
        item = self.ctx.library.hit_request(target.class_index)
        if item is None:
            return False
        hit = wire.HitRequest.unpack(item["payload"])._replace(
            slot=slot, room=self.ctx.peer.place.scene & 0xFF, vtable=self.vtables[target.class_index], forwarded=0)
        self.ctx.link.send(Msg.HIT_REQUEST, wire.HitRequest.pack(**hit._asdict()))
        return True

    # ---- simulator by replay -------------------------------------------------------------------------------------

    def start_replay(self, scene, clip=None, hits_to_kill=DEFAULT_HITS_TO_KILL):
        """Replays a recorded enemy clip of `scene` as the room's simulator (the fake must hold the room's claim)."""
        clip = clip or self.ctx.library.enemy_clip(scene)
        if clip is None:
            return False
        self._replay, self._scene, self._paused = clip, scene, False
        self._index, self._start = 0, time.monotonic()
        self.hits_to_kill = hits_to_kill
        self._replay_slots = {}
        return True

    def pause_replay(self, paused=True):
        """Holds (`cut`) or resumes the replay."""
        self._paused = paused

    def stop_replay(self):
        self._replay = None

    def on_hit_request(self, frame):
        hit = wire.HitRequest.unpack(frame.payload)
        self.hit_requests.append(hit)
        slot = self._replay_slots.get(hit.slot)
        if slot is None or slot.hp <= 0:
            return
        slot.detached, slot.hits = True, slot.hits + 1
        slot.hp = DEAD_HP if slot.hits >= self.hits_to_kill else max(slot.start_hp - slot.step * slot.hits, 1)

    def _play(self, now):
        frames = self._replay["frames"]
        elapsed_ms = (now - self._start) * 1000.0
        while self._index < len(frames) and frames[self._index]["dt"] <= elapsed_ms:
            item = frames[self._index]
            self._index += 1
            self._send_recorded(item)
        if self._index >= len(frames):
            self._index, self._start = 0, now

    def _send_recorded(self, item):
        scene = self._scene
        fields = dict(sender_ms=self.ctx.peer.sender_ms(), seq=self.ctx.peer.next("enemy_batch"),
                      room_epoch=self.ctx.authority.table.epoch_of(scene), scene=scene)
        if item["type"] == Msg.ENEMY_MOTION:
            _, entries = wire.unpack_enemy_batch(wire.EnemyMotionEntry, item["payload"])
            payload = wire.pack_enemy_batch(fields, wire.EnemyMotionEntry, [e._asdict() for e in entries])
            self.ctx.link.send(Msg.ENEMY_MOTION, payload)
            return
        _, entries = wire.unpack_enemy_batch(wire.EnemyPoseEntry, item["payload"])
        out, terminal = [], []
        for entry in entries:
            shown = None if self.ctx.contact.is_away(entry.slot) else self._apply_damage(entry)
            if shown is not None:
                (terminal if shown.flags & wire.ENEMY_TERMINAL else out).append(shown._asdict())
        if out:
            self.ctx.link.send(Msg.ENEMY_POSE, wire.pack_enemy_batch(fields, wire.EnemyPoseEntry, out), flags=0)
        if terminal:
            self.ctx.link.send(Msg.ENEMY_POSE, wire.pack_enemy_batch(dict(fields, seq=self.ctx.peer.next("enemy_batch")),
                                                                    wire.EnemyPoseEntry, terminal))

    def _apply_damage(self, entry):
        entry = entry._replace(flags=entry.flags & ~wire.ENEMY_LEASED, lease_token=0)
        slot = self._replay_slots.setdefault(entry.slot, ReplaySlot(entry, self.hits_to_kill))
        if not slot.detached:
            slot.entry = entry
            slot.hp = entry.hp
            return entry
        held = slot.entry._replace(hp=slot.hp)
        if slot.hp > 0:
            return held
        now = time.monotonic()
        if slot.terminal_sent:
            if now - slot.corpse_at < CORPSE_PERIOD_S:
                return None
            slot.corpse_at = now
            return held._replace(flags=0, hp=DEAD_HP)
        slot.terminal_sent, slot.corpse_at = True, now
        return held._replace(flags=wire.ENEMY_TERMINAL, hp=DEAD_HP)

    def replay_slot(self, slot):
        return self._replay_slots.get(slot)

    def nearest_replay_slot(self, pos, alive_only=True):
        """The replayed enemy standing closest to `pos` (and still alive), or None."""
        candidates = [(math.dist(slot.entry.pos, pos), number) for number, slot in self._replay_slots.items()
                      if not alive_only or slot.hp > 0]
        return min(candidates)[1] if candidates else None

    def replay_entries(self):
        """The last recorded entry of every replayed enemy, by slot."""
        return [self._replay_slots[slot].entry for slot in sorted(self._replay_slots)]

    def send_final(self, scene):
        """The simulator's last word before it leaves: one reliable round of every replayed enemy as it stands (alive or
        terminal), which the game that takes the room over adopts. False with nothing replayed."""
        entries = []
        for slot in self._replay_slots.values():
            held = slot.entry._replace(hp=slot.hp)
            entries.append((held if slot.hp > 0 else held._replace(flags=wire.ENEMY_TERMINAL, hp=DEAD_HP))._asdict())
        if entries:
            self.send_batch(scene, entries, reliable=True)
        return bool(entries)

    def tick(self, now):
        if self._replay and not self._paused and self.ctx.link.connected.is_set():
            self._play(now)

    # ---- batches the contact module streams ----------------------------------------------------------------------

    def send_batch(self, scene, entries, reliable=False):
        """ENEMY_POSE batches of dict entries for `scene` (split at 18), stamped with the fake's clock and the mirror's epoch."""
        epoch = self.ctx.authority.table.epoch_of(scene)
        for first in range(0, len(entries), wire.MAX_ENEMY_ENTRIES):
            fields = dict(sender_ms=self.ctx.peer.sender_ms(), seq=self.ctx.peer.next("enemy_batch"), room_epoch=epoch, scene=scene)
            payload = wire.pack_enemy_batch(fields, wire.EnemyPoseEntry, entries[first:first + wire.MAX_ENEMY_ENTRIES])
            self.ctx.link.send(Msg.ENEMY_POSE, payload, flags=wire.FLAG_RELIABLE if reliable else 0)

    # ---- wrong-binding probes ------------------------------------------------------------------------------------

    def probe(self, kind):
        """One batch the game must refuse: another spawn id (entry refused), an old epoch, or a sender that holds nothing.
        'slot' is the one it must accept: the right spawn record under another pool slot."""
        if kind not in PROBES:
            raise ValueError(f"unknown probe {kind}")
        scene = self.ctx.peer.place.scene
        clip = self._replay or self.ctx.library.enemy_clip(scene)
        entries = [e for item in clip["frames"] if item["type"] == Msg.ENEMY_POSE
                   for e in wire.unpack_enemy_batch(wire.EnemyPoseEntry, item["payload"])[1]][:1]
        if not entries:
            return False
        entry = entries[0]._asdict()
        epoch = self.ctx.authority.table.epoch_of(scene)
        if kind == "binding":
            entry["spawn_id"] = (entry["spawn_id"] + SPAWN_ID_STEP) & 0xFFFF
        if kind == "epoch":
            epoch = (epoch - 1) & 0xFFFFFFFF
        if kind == "slot":
            entry["slot"] = (entry["slot"] + 1) % POOL_SLOTS
        fields = dict(sender_ms=self.ctx.peer.sender_ms(), seq=self.ctx.peer.next("enemy_batch"), room_epoch=epoch, scene=scene)
        self.ctx.link.send(Msg.ENEMY_POSE, wire.pack_enemy_batch(fields, wire.EnemyPoseEntry, [entry]), flags=0)
        return True
