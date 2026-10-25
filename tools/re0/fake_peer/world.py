"""Shared world state in both seats: the host item ledger (TAKE_REQUEST / TAKE_RESULT / FLOOR_PUT / FLOOR_TAKE), the story
flag sequencer (FLAG_PROPOSE / FLAG_COMMIT), the push block leases (PROP_LEASE / PROP_MOVE / PROP_COMMIT) and the world digest
(WORLD_DIGEST) the fake sends from those mirrors.

Each part keeps a mirror of the shared table (ledger, flag words, prop journal) fed by both sides' messages; the mirror is
the fake's own state and an assertion source. Item keys, flag bits and box poses are never invented: they come from what the
game sent or the library."""
import math
import struct
import time
import zlib

from fake_peer import wire
from fake_peer.link import Module
from fake_peer.wire import Msg

SAME_ITEM_DISTANCE = 10.0
RESERVATION_S = 15.0
MAX_LEDGER_ENTRIES = 256
PICKUP_MOTION, BODY_LAYER, BODY_TARGET = 0x14, 0, 0
FLAGS_OFFSET = 2 * wire.PLACE_SIZE + 128  # JOIN_SNAPSHOT: two places, two inventories, then the flag words
MOVE_HZ = 10.0
LEASE_IDLE_S = 4.0
PUSH_STEPS = 10
PUSH_OFFSET = (60.0, 0.0, 0.0)
LAPSE_S = 5.0
DIGEST_PERIOD_S = 1.0
MASK32 = 0xFFFFFFFF
PRESENCE_FLAGS = (0x37, 0x93, 0x10D)  # event_rule::kPresenceFlags: each machine's own, left out of a digest
ENEMY_FLAGS = range(0x312, 0x412)     # flag_family: the enemy family
ACT_FLAGS = range(0x612, 0x712)       # flag_family: the act family
FNV_OFFSET, FNV_PRIME = 2166136261, 16777619


# ---- items ------------------------------------------------------------------------------------------------------------

def same_item(a, b):
    return a[0] == b[0] and a[1] == b[1] and math.dist(a[2], b[2]) <= SAME_ITEM_DISTANCE


class Ledger:
    """item_ledger_rule::Ledger: the first request reserves the item for its slot, later ones are claimed, a taken item is gone."""

    def __init__(self):
        self.entries = []  # dicts: key, consumed, holder, expires, serial
        self.serial = 0

    def _find(self, key):
        return next((e for e in self.entries if same_item(e["key"], key)), None)

    def _add(self, key, consumed, holder, now):
        if len(self.entries) >= MAX_LEDGER_ENTRIES:
            self.entries.pop(0)
        self.serial += 1
        entry = dict(key=key, consumed=consumed, holder=holder, expires=now + RESERVATION_S, serial=self.serial)
        self.entries.append(entry)
        return entry

    def request(self, key, slot, now):
        """(answer, serial) for a take request."""
        entry = self._find(key)
        if entry is None:
            return wire.ANSWER_GRANTED, self._add(key, False, slot, now)["serial"]
        if entry["consumed"]:
            return wire.ANSWER_TAKEN, entry["serial"]
        if entry["holder"] == slot:
            entry["expires"] = now + RESERVATION_S
            return wire.ANSWER_GRANTED, entry["serial"]
        if now > entry["expires"]:
            self.entries.remove(entry)
            return wire.ANSWER_GRANTED, self._add(key, False, slot, now)["serial"]
        return wire.ANSWER_CLAIMED, entry["serial"]

    def taken(self, key):
        entry = self._find(key)
        if entry:
            entry["consumed"] = True
        else:
            self._add(key, True, 0, 0.0)

    def put(self, key):
        self.entries = [e for e in self.entries if not same_item(e["key"], key)]

    def consumed_keys(self):
        return [e["key"] for e in self.entries if e["consumed"]]


class Items(Module):
    HANDLES = {Msg.TAKE_REQUEST: "on_request", Msg.TAKE_RESULT: "on_result", Msg.FLOOR_PUT: "on_put",
               Msg.FLOOR_TAKE: "on_take", Msg.MOTION_EVENT: "on_motion"}

    def __init__(self, ctx):
        super().__init__(ctx)
        self.ledger = Ledger()
        self.floor = {}  # key -> count: items the game named
        self.answer_ms = 0.0
        self.forced = None
        self.results = []   # (seq, answer name) the fake received as the asker
        self.answered = []  # (slot, answer name) the fake gave as the ledger
        self.race_armed = None  # a key to request the moment the game's pickup motion starts
        self._pending = {}
        self._due = []

    def learn(self, scene, item_id, pos, count=1):
        self.floor[(scene, item_id, tuple(pos))] = count

    def learn_floor(self, entries):
        for entry in entries:
            if not entry.is_take:
                self.learn(entry.room, entry.item_id, entry.pos, entry.count)

    def adopt_consumed(self, items):
        for item in items:
            self.ledger.taken((item.scene, item.item_id, tuple(item.pos)))

    def floor_for_snapshot(self):
        return [dict(room=k[0], item_id=k[1], count=c, pos=k[2]) for k, c in self.floor.items()]

    def consumed_dicts(self):
        return [dict(scene=k[0], item_id=k[1], pos=k[2]) for k in self.ledger.consumed_keys()]

    def known_keys(self, scene=None):
        keys = list(self.floor) + [(i["scene"], i["item_id"], i["pos"]) for i in self.ctx.library.floor_items]
        return [k for k in keys if scene is None or k[0] == scene]

    def on_put(self, frame):
        put = wire.FloorPut.unpack(frame.payload)
        self.learn(put.room, put.item_id, put.pos, put.count)
        self.ledger.put((put.room, put.item_id, tuple(put.pos)))

    def on_take(self, frame):
        take = wire.FloorTake.unpack(frame.payload)
        self.floor.pop((take.room, take.item_id, tuple(take.pos)), None)
        self.ledger.taken((take.room, take.item_id, tuple(take.pos)))

    # host seat: the ledger

    def on_request(self, frame):
        if not self.ctx.peer.is_host:
            return
        request = wire.TakeRequest.unpack(frame.payload)
        key = (request.scene, request.item_id, tuple(request.pos))
        self.learn(*key)
        answer, serial = self.ledger.request(key, frame.slot, time.monotonic())
        if self.forced == "none":
            return
        answer = {"claimed": wire.ANSWER_CLAIMED, "taken": wire.ANSWER_TAKEN}.get(self.forced, answer)
        payload = wire.TakeResult.pack(serial=serial, seq=request.seq, scene=request.scene, requester=frame.slot, answer=answer)
        self._due.append((time.monotonic() + self.answer_ms / 1000.0, payload))
        self.answered.append((frame.slot, answer))

    # guest seat: asking

    def take(self, key=None):
        """TAKE_REQUEST for a floor item the game named (the first known one when no key is given)."""
        key = key or next(iter(self.known_keys(self.ctx.peer.place.scene)), None)
        if key is None:
            return None
        seq = self.ctx.peer.next_u16("take")
        self._pending[seq] = key
        self.ctx.link.send(Msg.TAKE_REQUEST, wire.TakeRequest.pack(seq=seq, scene=key[0], item_id=key[1], count=1, pos=key[2],
                                                                       world_epoch=self.ctx.flows.world_epoch))
        return key

    def race(self, key=None):
        """Requests the item the moment the game's pickup motion shows (its request then reaches the ledger first)."""
        self.race_armed = key or next(iter(self.known_keys(self.ctx.peer.place.scene)), None)
        return self.race_armed is not None

    def on_motion(self, frame):
        event = wire.MotionEvent.unpack(frame.payload)
        if (self.race_armed and event.motion_no == PICKUP_MOTION and event.layer & 0xFF == BODY_LAYER
                and event.layer >> 8 == BODY_TARGET):
            key, self.race_armed = self.race_armed, None
            self.take(key)

    def on_result(self, frame):
        result = wire.TakeResult.unpack(frame.payload)
        if result.requester != self.ctx.peer.slot or result.seq not in self._pending:
            return
        key = self._pending.pop(result.seq)
        self.results.append((result.seq, result.answer))
        if result.answer == wire.ANSWER_GRANTED:
            self.ctx.link.send(Msg.FLOOR_TAKE, wire.FloorTake.pack(room=key[0], item_id=key[1], pos=key[2],
                                                                    world_epoch=self.ctx.flows.world_epoch))

    def tick(self, now):
        while self._due and self._due[0][0] <= now:
            self.ctx.link.send(Msg.TAKE_RESULT, self._due.pop(0)[1])


# ---- flags ------------------------------------------------------------------------------------------------------------

class Flags(Module):
    HANDLES = {Msg.FLAG_PROPOSE: "on_propose", Msg.FLAG_COMMIT: "on_commit"}

    def __init__(self, ctx):
        super().__init__(ctx)
        self.words = [0] * wire.FLAG_WORDS
        self.serial = 0        # host: the last commit it made; guest: the last one it saw
        self.violations = []
        self.commits = []      # (header, changes) seen
        self.proposed = []     # changes lists the fake proposed, oldest first (awaiting their commit)
        self.proposals_sent = 0
        self.cross = False
        self.correct_next = False
        self.gap_next = False
        self.corrected = 0

    def adopt_words(self, snapshot):
        self.words = list(struct.unpack_from(f"<{wire.FLAG_WORDS}I", snapshot, FLAGS_OFFSET))

    def adopt_serial(self, serial):
        self.serial = serial

    def _apply(self, changes):
        effective = []
        for change in changes:
            before = self.words[change.word]
            after = (before | change.set) & ~change.clear
            if after != before:
                effective.append(dict(word=change.word, set=after & ~before, clear=before & ~after))
                self.words[change.word] = after
        return effective

    def _noop(self):
        return dict(word=0, set=1, clear=0) if self.words[0] & 1 else dict(word=0, set=0, clear=1)

    def propose(self, changes=None):
        """A FLAG_PROPOSE; the default is a no-op (a bit set to the value the mirror already holds)."""
        changes = changes or [self._noop()]
        self.proposed.append(changes)
        self.proposals_sent += 1
        self.ctx.link.send(Msg.FLAG_PROPOSE, wire.pack_flag_propose(changes, self.ctx.flows.world_epoch))

    # host seat

    def on_propose(self, frame):
        if not self.ctx.peer.is_host:
            return
        changes = wire.unpack_flag_propose(frame.payload)
        corrected = self.correct_next and bool(changes)
        if corrected:  # the commit carries the opposite value even where the host's mirror already holds it
            self.correct_next = False
            changes = [wire.FlagChange.make(word=c.word, set=c.clear, clear=c.set) for c in changes]
        effective = self._apply(changes)
        shown = [dict(word=c.word, set=c.set, clear=c.clear) for c in changes] if corrected else effective
        self.serial += 2 if self.gap_next else 1
        self.gap_next = False
        self.ctx.link.send(Msg.FLAG_COMMIT, wire.pack_flag_commit(self.serial, frame.slot, shown))

    def commit_own(self, changes):
        """The host's own change: applied, then committed with origin 0."""
        effective = self._apply([wire.FlagChange.make(**c) for c in changes])
        self.serial += 1
        self.ctx.link.send(Msg.FLAG_COMMIT, wire.pack_flag_commit(self.serial, self.ctx.peer.slot, effective))

    # guest seat

    def on_commit(self, frame):
        if self.ctx.peer.is_host:
            return
        header, changes = wire.unpack_flag_commit(frame.payload)
        if self.serial and header.serial != self.serial + 1:
            self.violations.append(f"commit {header.serial} after {self.serial}")
        self.serial = header.serial
        self.commits.append((header, changes))
        mine = header.origin == self.ctx.peer.slot
        if mine and self.proposed:
            self.proposed.pop(0)
        effective = self._apply(changes)
        if self.cross and effective and not mine:
            self.propose([dict(word=c["word"], set=c["set"], clear=c["clear"]) for c in effective[:1]])


# ---- props ------------------------------------------------------------------------------------------------------------

class Props(Module):
    HANDLES = {Msg.PROP_LEASE: "on_lease", Msg.PROP_MOVE: "on_move", Msg.PROP_COMMIT: "on_commit"}

    def __init__(self, ctx):
        super().__init__(ctx)
        self.boxes = {}      # (scene, unit) -> last pose (pos, yaw) the game sent or committed
        self.commits = {}    # (scene, unit) -> PropCommit record
        self.leases = {}     # (scene, unit) -> {"holder", "serial", "active"}
        self.moves = []      # (key, sender_ms, pos) taken from the holder
        self.deny = False
        self.last_push_pos = None  # the final pose of the fake's last push
        self.denied = []     # (key, holder) Deny records received
        self.granted = []    # keys the fake was granted
        self.violations = []
        self._serial = 0
        self._push = None

    def adopt(self, commits):
        for commit in commits:
            key = (commit.scene, commit.unit)
            self.commits[key] = commit
            self.boxes[key] = (tuple(commit.pos), commit.yaw)

    def commit_dicts(self):
        return [c._asdict() for c in self.commits.values()]

    def box_pose(self, scene, unit):
        pose = self.boxes.get((scene, unit))
        if pose:
            return pose
        known = self.ctx.library.box(scene, unit)
        return (tuple(known["pos"]), known["yaw"]) if known else None

    # host seat

    def on_lease(self, frame):
        lease = wire.PropLease.unpack(frame.payload)
        key = (lease.scene, lease.unit)
        peer = self.ctx.peer
        if lease.op in (wire.LEASE_GRANT, wire.LEASE_DENY):
            self._on_answer(key, lease)
        elif peer.is_host and lease.op == wire.LEASE_REQUEST:
            self._decide(key, frame.slot)
        elif peer.is_host and lease.op == wire.LEASE_RELEASE:
            self._release(key, frame.slot)

    def _decide(self, key, slot):
        held = self.leases.get(key)
        busy = held and time.monotonic() - held["active"] < LEASE_IDLE_S and held["holder"] != slot
        grant = not self.deny and not busy
        if grant:
            self._serial += 1
            self.leases[key] = dict(holder=slot, serial=self._serial, active=time.monotonic())
        holder = slot if grant else (held["holder"] if held else self.ctx.peer.slot)
        serial = self.leases[key]["serial"] if grant else (held["serial"] if held else 0)
        self.ctx.link.send(Msg.PROP_LEASE, wire.PropLease.pack(
            serial=serial, scene=key[0], unit=key[1], op=wire.LEASE_GRANT if grant else wire.LEASE_DENY, holder=holder))

    def _release(self, key, slot):
        held = self.leases.get(key)
        if not held or held["holder"] != slot:
            return
        pose = self.boxes.get(key)
        del self.leases[key]
        if pose is None:
            return
        commit = wire.PropCommit.make(serial=held["serial"], scene=key[0], kind=wire.PROP_OBSTACLE_KIND, unit=key[1],
                                      pos=pose[0], yaw=pose[1], committer=slot)
        self.commits[key] = commit
        self.ctx.link.send(Msg.PROP_COMMIT, wire.PropCommit.pack(**commit._asdict()))

    def on_move(self, frame):
        move = wire.PropMove.unpack(frame.payload)
        key = (move.scene, move.unit)
        held = self.leases.get(key)
        if self.ctx.peer.is_host:
            if not held or held["holder"] != frame.slot:
                self.violations.append(f"move for {key} from slot {frame.slot} without a lease")
                return
            held["active"] = time.monotonic()
        self.boxes[key] = (tuple(move.pos), move.yaw)
        self.moves.append((key, move.sender_ms, tuple(move.pos)))

    def on_commit(self, frame):
        commit = wire.PropCommit.unpack(frame.payload)
        key = (commit.scene, commit.unit)
        self.commits[key] = commit
        self.boxes[key] = (tuple(commit.pos), commit.yaw)

    # guest seat: pushing

    def push(self, scene, unit, lapse=False):
        """Requests the box; on a grant streams it along a short path from its last known pose, then releases it."""
        pose = self.box_pose(scene, unit)
        if pose is None:
            return False
        self._push = dict(key=(scene, unit), start=pose[0], yaw=pose[1], state="requested", lapse=lapse)
        self.ctx.link.send(Msg.PROP_LEASE, wire.PropLease.pack(scene=scene, unit=unit, op=wire.LEASE_REQUEST,
                                                               holder=self.ctx.peer.slot))
        return True

    def _on_answer(self, key, lease):
        push = self._push
        if lease.holder == self.ctx.peer.slot and lease.op == wire.LEASE_GRANT:
            self.granted.append(key)
            if push and push["key"] == key:
                push.update(state="moving", step=0, next=time.monotonic(), granted_at=time.monotonic())
        elif lease.op == wire.LEASE_DENY:
            self.denied.append((key, lease.holder))
            if push and push["key"] == key:
                self._push = None

    def _send_move(self, push, fraction, reliable=False):
        start = push["start"]
        pos = tuple(s + o * fraction for s, o in zip(start, PUSH_OFFSET))
        key = push["key"]
        if reliable:
            self.last_push_pos = pos
        self.ctx.link.send(Msg.PROP_MOVE, wire.PropMove.pack(
            sender_ms=self.ctx.peer.sender_ms(), scene=key[0], kind=wire.PROP_OBSTACLE_KIND, unit=key[1], pos=pos, yaw=push["yaw"]),
            flags=wire.FLAG_RELIABLE if reliable else 0)

    def tick(self, now):
        push = self._push
        if not push or push["state"] != "moving":
            return
        if push["lapse"]:
            if now - push["granted_at"] > LAPSE_S:
                self._push = None
            return
        if now < push["next"]:
            return
        push["next"] = now + 1.0 / MOVE_HZ
        push["step"] += 1
        last = push["step"] >= PUSH_STEPS
        self._send_move(push, push["step"] / PUSH_STEPS, reliable=last)
        if last:
            key = push["key"]
            self.ctx.link.send(Msg.PROP_LEASE, wire.PropLease.pack(scene=key[0], unit=key[1], op=wire.LEASE_RELEASE,
                                                                   holder=self.ctx.peer.slot))
            self._push = None


# ---- world digest -----------------------------------------------------------------------------------------------------

def flag_crcs(words):
    """world_digest_rule::flagCrcs: CRC-32 of the story bits (every family but enemy and act), the enemy family and the act
    family, with the presence flags cleared."""
    story = list(words)
    for bit in PRESENCE_FLAGS:
        story[bit // 32] &= ~(1 << bit % 32) & MASK32
    enemy, act = [0] * len(story), [0] * len(story)
    for family, bits in ((enemy, ENEMY_FLAGS), (act, ACT_FLAGS)):
        for bit in bits:
            mask = 1 << bit % 32
            family[bit // 32] |= story[bit // 32] & mask
            story[bit // 32] &= ~mask & MASK32
    return tuple(zlib.crc32(struct.pack(f"<{len(part)}I", *part)) for part in (story, enemy, act))


def authority_hash(records):
    """world_digest_rule::authorityHash: FNV-1a over (scene u16, owner u8, epoch u32) of every owned room in scene order."""
    hashed = FNV_OFFSET
    for record in sorted((r for r in records if r.owner != wire.NO_OWNER), key=lambda r: r.id):
        for value, size in ((record.id, 2), (record.owner, 1), (record.epoch, 4)):
            for byte in value.to_bytes(size, "little"):
                hashed = ((hashed ^ byte) * FNV_PRIME) & MASK32
    return hashed


class Digest(Module):
    """WORLD_DIGEST: once started, one per second built from the flag and authority mirrors. The fake keeps no floor journal or
    load serial, so those two are the game's own newest values. `flip` toggles one flag bit in what is sent, not in the mirror."""

    HANDLES = {Msg.WORLD_DIGEST: "on_digest"}

    def __init__(self, ctx):
        super().__init__(ctx)
        self.sending = False
        self.flip_bit = None
        self.game = None  # the game's newest WorldDigest record
        self.received = 0
        self.sent = 0
        self._next = 0.0

    def start(self):
        self.sending, self._next = True, 0.0

    def stop(self):
        self.sending, self.flip_bit = False, None

    def flip(self, bit=ENEMY_FLAGS.start):
        self.flip_bit = bit

    def build(self):
        flags = self.ctx.flags
        words = list(flags.words)
        if self.flip_bit is not None:
            words[self.flip_bit // 32] ^= 1 << self.flip_bit % 32
        story, enemy, act = flag_crcs(words)
        game = self.game
        return wire.WorldDigest.pack(
            flag_serial=flags.serial, flag_crc=story, enemy_flag_crc=enemy, act_flag_crc=act,
            authority_hash=authority_hash(self.ctx.authority.table.records.values()),
            load_serial=game.load_serial if game else 0,
            pending_proposals=len(flags.proposed), flags=wire.DIGEST_CAUGHT_UP, scene=self.ctx.peer.place.scene,
            sender_ms=self.ctx.peer.sender_ms())

    def on_digest(self, frame):
        self.game = wire.WorldDigest.unpack(frame.payload)
        self.received += 1

    def tick(self, now):
        if not self.sending or now < self._next:
            return
        self._next = now + DIGEST_PERIOD_S
        if self.ctx.link.send(Msg.WORLD_DIGEST, self.build(), flags=0):
            self.sent += 1
