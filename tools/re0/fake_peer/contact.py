"""Contact and lease: PLAYER_HIT, ENEMY_LEASE and ENEMY_LEASE_END from both seats.

Victim seat (the game simulates, its enemy reaches the fake's puppet): the fake validates the hits, lowers its HP by a named
step and answers a grant by a lease mode (hold, decline, expire, silent, bad-token, bad-hp, jump, cut).
Authority seat (the fake owns the room by replay, the game's character is the victim): the fake sends plain hits and grabs,
and judges the game's leased poses by the authority's acceptance rule (holder, token, HP never above the grant, a bounded
displacement) until the lease ends or lapses."""
import math
import time

from fake_peer import wire
from fake_peer.link import Module
from fake_peer.wire import Msg

HIT_HP_STEP = 10
LEASE_HITS_TO_KILL = 3
HOLD_START_S = 0.1
DEFAULT_HOLD_S = 3.0
LEASED_POSE_HZ = 20.0
CUT_S = 2.0
GRANT_EXPIRY_S = 1.5
LATE_POSE_S = 1.0  # a leased pose may trail its lease end: the reliable end passes a held unreliable pose
HOLDER_SILENCE_S = 2.5
POSE_BASE_UNITS = 1000.0
POSE_UNITS_PER_SECOND = 12000.0
POSE_MAX_DT_S = 2.5
JUMP_UNITS = 50000.0
GRAB_DISTANCE = 60.0
HIT_POINT_UP = 100.0
THINK_RECORD = (2, 0, 0, 0)  # state 2, action 0: the class's own think picks the next action
ALLOWED_HITS = ((0, 2), (0, 3))  # (kind, level) of plain reaction entries; never 4 or 6 (death state 19)
LEASE_MODES = ("hold", "decline", "expire", "silent", "bad-token", "bad-hp", "jump", "cut")
RELEASING_MODES = ("hold", "cut")
TOKEN_SLOT_SHIFT = 16


def token_low(token):
    return token & 0xFFFF


def accept_holder_pose(previous, pos, dt_s, hp, lease_hp):
    """contact_rule::acceptHolderPose."""
    if hp > lease_hp:
        return False
    return math.dist(previous, pos) <= POSE_BASE_UNITS + POSE_UNITS_PER_SECOND * min(dt_s, POSE_MAX_DT_S)


class Held:
    """A lease the game gave the fake (victim seat): the fake streams the enemy and ends the lease."""

    def __init__(self, grant, mode, now):
        self.grant, self.mode = grant, mode
        self.token = grant.token
        self.hp = grant.hp
        self.start_at = now + HOLD_START_S
        self.ends_at = None
        self.last_pose = 0.0
        self.streamed = 0


class Away:
    """A lease the fake gave the game (authority seat): the fake judges the game's leased poses."""

    def __init__(self, grant, now):
        self.grant = grant
        self.token, self.slot, self.hp = grant.token, grant.slot, grant.hp
        self.last_pos = tuple(grant.pos)
        self.started = self.heard_at = now
        self.heard = False


class Contact(Module):
    HANDLES = {Msg.PLAYER_HIT: "on_player_hit", Msg.ENEMY_LEASE: "on_grant", Msg.ENEMY_LEASE_END: "on_end",
               Msg.HIT_REQUEST: "on_hit_request"}

    def __init__(self, ctx):
        super().__init__(ctx)
        self._lease_mode = "hold"
        self.hold_s = DEFAULT_HOLD_S
        self.hits = []          # PlayerHit records the game sent that the fake accepted
        self.refused_hits = []  # (record, reason)
        self.held = {}          # slot -> Held
        self.away = {}          # slot -> Away
        self.grants = []        # EnemyLease records the game sent
        self.ends = []          # (EnemyLeaseEnd record, who) both directions
        self.refused_poses = []  # (slot, reason) holder poses the fake's rule refused
        self._ended = {}         # low token bits -> when the lease ended
        self.accepted_poses = 0
        self._hit_seq = 0
        self._token = 0

    @property
    def lease_mode(self):
        return self._lease_mode

    @lease_mode.setter
    def lease_mode(self, mode):
        if mode not in LEASE_MODES:
            raise ValueError(f"unknown lease mode {mode}")
        self._lease_mode = mode

    # ---- victim seat ---------------------------------------------------------------------------------------------

    def on_player_hit(self, frame):
        hit = wire.PlayerHit.unpack(frame.payload)
        reason = self._hit_refusal(hit, frame.slot)
        if reason:
            self.refused_hits.append((hit, reason))
            return
        self._hit_seq = hit.seq
        self.hits.append(hit)
        self.ctx.peer.hp = max(self.ctx.peer.hp - HIT_HP_STEP, 0)

    def _hit_refusal(self, hit, sender):
        table = self.ctx.authority.table
        if hit.room_epoch != table.epoch_of(hit.scene):
            return "epoch"
        if hit.scene != self.ctx.peer.place.scene:
            return "scene"
        if table.owner_of(hit.scene) != sender:
            return "sender"
        if hit.victim != self.ctx.peer.character:
            return "victim"
        if hit.seq <= self._hit_seq:
            return "seq"
        return None

    def on_grant(self, frame):
        grant = wire.EnemyLease.unpack(frame.payload)
        self.grants.append(grant)
        if self.lease_mode == "decline":
            self._send_end(grant, wire.END_DECLINED)
        elif self.lease_mode == "expire":
            return
        else:
            self.held[grant.slot] = Held(grant, self.lease_mode, time.monotonic())
        if self.lease_mode == "cut":
            self.ctx.shaper.cut(CUT_S)

    def on_hit_request(self, frame):
        hit = wire.HitRequest.unpack(frame.payload)
        held = self.held.get(hit.slot)
        if held is None:
            return
        held.hp -= max(held.grant.hp // LEASE_HITS_TO_KILL, 1)
        if held.hp <= 0:
            self._end_held(held, wire.END_ENEMY_DEAD)

    def _entry(self, held, hp=None, pos=None, token=None):
        grant = held.grant
        return dict(slot=grant.slot, class_index=grant.class_index, flags=wire.ENEMY_ALIVE | wire.ENEMY_LEASED,
                    target=grant.victim, spawn_id=grant.spawn_id, hp=held.hp if hp is None else hp,
                    pos=grant.pos if pos is None else pos, quat=grant.quat, motion0=wire.NO_MOTION, motion1=wire.NO_MOTION,
                    lease_token=token_low(grant.token if token is None else token), record=grant.record)

    def _stream(self, held, now):
        if held.mode == "bad-token":
            entry = self._entry(held, token=held.token ^ 1)
        elif held.mode == "bad-hp":
            entry = self._entry(held, hp=held.grant.hp + 1)
        elif held.mode == "jump":
            entry = self._entry(held, pos=(held.grant.pos[0] + JUMP_UNITS, held.grant.pos[1], held.grant.pos[2]))
        else:
            entry = self._entry(held)
        self.ctx.enemies.send_batch(held.grant.scene, [entry])
        held.streamed += 1
        held.last_pose = now

    def _send_end(self, grant, reason, hp=None, record=None):
        self.ctx.link.send(Msg.ENEMY_LEASE_END, wire.EnemyLeaseEnd.pack(
            token=grant.token, room_epoch=self.ctx.authority.table.epoch_of(grant.scene), scene=grant.scene, slot=grant.slot,
            reason=reason, hp=grant.hp if hp is None else hp, record=grant.record if record is None else record,
            pos=grant.pos, quat=grant.quat))

    def _end_held(self, held, reason):
        self.held.pop(held.grant.slot, None)
        self._send_end(held.grant, reason, hp=max(held.hp, 0), record=THINK_RECORD)

    def _tick_held(self, now):
        for held in list(self.held.values()):
            if now < held.start_at:
                continue
            if held.ends_at is None:
                held.ends_at = now + self.hold_s
            if held.mode == "silent":
                if not held.streamed:
                    self._stream(held, now)
            elif now < held.ends_at:
                if now - held.last_pose >= 1.0 / LEASED_POSE_HZ:
                    self._stream(held, now)
            elif held.mode in RELEASING_MODES:
                self._end_held(held, wire.END_RELEASED)
            else:
                del self.held[held.grant.slot]  # the refused modes just stop; the authority's rule ends the lease

    # ---- authority seat ------------------------------------------------------------------------------------------

    def hit(self, slot, kind=0, level=2):
        """A plain PLAYER_HIT for a replayed enemy, from the allow list of reaction entries."""
        if (kind, level) not in ALLOWED_HITS:
            raise ValueError(f"hit kind {kind} level {level} is not a plain entry")
        entry = self.ctx.enemies.replay_slot(slot)
        if entry is None:
            return False
        e, peer = entry.entry, self.ctx.peer
        self.ctx.link.send(Msg.PLAYER_HIT, wire.PlayerHit.pack(
            room_epoch=self.ctx.authority.table.epoch_of(peer.place.scene), sender_ms=peer.sender_ms(), seq=peer.next("player_hit"),
            scene=peer.place.scene, slot=slot, class_index=e.class_index, spawn_id=e.spawn_id, victim=peer.game_character,
            kind=kind, level=level, point=(0.0, 0.0, HIT_POINT_UP)))
        return True

    def grab(self, slot):
        """ENEMY_LEASE from the lease library for the class of a replayed enemy, the enemy 60 units from the game's character."""
        replay = self.ctx.enemies.replay_slot(slot)
        if replay is None:
            return False
        item = self.ctx.library.lease_grant(replay.entry.class_index)
        if item is None:
            return False
        peer = self.ctx.peer
        victim = self.ctx.remote.position()
        pos = (victim[0] + GRAB_DISTANCE, victim[1], victim[2])
        self._token = (self._token + 1) % 0xFFFF + 1
        token = (peer.slot + 1) << TOKEN_SLOT_SHIFT | self._token
        recorded = wire.EnemyLease.unpack(item["payload"])
        grant = recorded._replace(token=token, room_epoch=self.ctx.authority.table.epoch_of(peer.place.scene),
                                  sender_ms=peer.sender_ms(), scene=peer.place.scene, slot=slot,
                                  spawn_id=replay.entry.spawn_id, victim=peer.game_character, pos=pos)
        self.ctx.link.send(Msg.ENEMY_LEASE, wire.EnemyLease.pack(**grant._asdict()))
        self.away[slot] = Away(grant, time.monotonic())
        return True

    def expire_holder(self, slot):
        """ENEMY_LEASE_END Expired from the authority while the game holds the lease."""
        away = self.away.pop(slot, None)
        if away is None:
            return False
        self._send_end(away.grant, wire.END_EXPIRED)
        return True

    def on_holder_batch(self, header, entries, frame):
        """The game's poses for enemies the fake gave away (the fake owns the room): the authority's acceptance rule."""
        now = time.monotonic()
        for entry in entries:
            away = self.away.get(entry.slot)
            if away is None and entry.flags & wire.ENEMY_LEASED and now - self._ended.get(entry.lease_token, -LATE_POSE_S) < LATE_POSE_S:
                continue
            if away is None or not entry.flags & wire.ENEMY_LEASED:
                self.refused_poses.append((entry.slot, "no lease"))
            elif token_low(away.token) != entry.lease_token:
                self.refused_poses.append((entry.slot, "wrong token"))
            elif not accept_holder_pose(away.last_pos, entry.pos, now - (away.heard_at if away.heard else away.started),
                                        entry.hp, away.hp):
                self.refused_poses.append((entry.slot, "hp or displacement"))
            else:
                away.last_pos, away.hp, away.heard, away.heard_at = tuple(entry.pos), min(away.hp, entry.hp), True, now
                self.accepted_poses += 1

    def on_end(self, frame):
        end = wire.EnemyLeaseEnd.unpack(frame.payload)
        self.ends.append((end, "game"))
        away = self.away.get(end.slot)
        if away and away.token == end.token:
            self._ended[token_low(end.token)] = time.monotonic()
            del self.away[end.slot]

    def _tick_away(self, now):
        for slot, away in list(self.away.items()):
            lapsed = (not away.heard and now - away.started > GRANT_EXPIRY_S) or (away.heard and now - away.heard_at > HOLDER_SILENCE_S)
            if lapsed:
                self._send_end(away.grant, wire.END_SILENT if away.heard else wire.END_EXPIRED)
                self._ended[token_low(away.token)] = now
                del self.away[slot]

    # ---- shared --------------------------------------------------------------------------------------------------

    def lease_exists(self, token_low_bits):
        """A lease the fake holds or knows about carries these low 16 token bits."""
        tokens = [h.token for h in self.held.values()] + [a.token for a in self.away.values()]
        return any(token_low(t) == token_low_bits for t in tokens)

    def is_away(self, slot):
        return slot in self.away

    def tick(self, now):
        self._tick_held(now)
        self._tick_away(now)
