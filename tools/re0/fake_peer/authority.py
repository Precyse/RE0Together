"""The room authority mirror: DS2's AuthorityTable rules in Python (adapters/ds2/src/authority.cpp) and the fake's own
claim / release / decline actions (adapters/re0/src/room_authority_rule.h reconcile)."""
import time
from collections import namedtuple

from fake_peer import wire
from fake_peer.link import Module
from fake_peer.wire import NO_OWNER, Msg

APPLIED, DUPLICATE, STALE_EPOCH, UNKNOWN_OBJECT, WRONG_SENDER, INVALID_OWNER = (
    "applied", "duplicate", "stale epoch", "unknown object", "wrong sender", "invalid owner")
CLAIM, DECLINE, STOP = Msg.ROOM_CLAIM, Msg.ROOM_DECLINE, Msg.ROOM_STOP
RECONCILE_PERIOD_S = 0.1
DEFAULT_CLAIM_DELAY_S = 0.3  # a real adapter claims on a game tick after the arrival; the game's own claim usually lands first
MODES = ("normal", "tie", "first", "force")

Auth = namedtuple("Auth", "id epoch owner")


class Record:
    def __init__(self, scene, owner, epoch):
        self.id, self.owner, self.epoch, self.barred = scene, owner, epoch, set()


class AuthorityTable:
    """Who simulates each room and the room's generation; a stale or duplicated message is harmless."""

    def __init__(self):
        self.local = NO_OWNER
        self.host = NO_OWNER
        self.records = {}
        self.changes = []  # (wall ms, scene, owner, epoch) after each change: the mirror the game's log is compared with

    def set_session(self, local, host):
        self.local, self.host = local, host

    def clear(self):
        self.records.clear()

    # local actions: each returns the (type, Auth) to send to everyone, or None when the rules refuse it

    def claim(self, scene):
        return self.assign(scene, self.local)

    def assign(self, scene, owner):
        if self.local == NO_OWNER or owner == NO_OWNER or (owner != self.local and self.local != self.host):
            return None
        record = self.records.get(scene)
        if record and (record.owner == owner or (record.owner != NO_OWNER and self.local != self.host)):
            return None
        message = Auth(scene, 1 if record is None else record.epoch + 1, owner)
        return (CLAIM, message) if self.on_message(CLAIM, self.local, message) == APPLIED else None

    def release(self, scene):
        record = self.records.get(scene)
        if self.local == NO_OWNER or not record or record.owner != self.local:
            return None
        message = Auth(scene, record.epoch, self.local)
        return (STOP, message) if self.on_message(STOP, self.local, message) == APPLIED else None

    def stop(self, scene):
        record = self.records.get(scene)
        if self.local != self.host or not record or record.owner == NO_OWNER:
            return None
        message = Auth(scene, record.epoch, record.owner)
        return (STOP, message) if self.on_message(STOP, self.local, message) == APPLIED else None

    def decline(self, scene):
        record = self.records.get(scene)
        if self.local == NO_OWNER or not record or record.owner != self.local:
            return None
        message = Auth(scene, record.epoch, self.local)
        return (DECLINE, message) if self.on_message(DECLINE, self.local, message) == APPLIED else None

    def on_message(self, kind, sender, message):
        if kind == CLAIM:
            return self._claim(sender, message)
        if kind == DECLINE:
            return self._decline(sender, message)
        return self._stop(sender, message)

    def _claim(self, sender, message):
        if message.owner != sender and sender != self.host:
            return WRONG_SENDER
        record = self.records.get(message.id)
        if record is None:
            record = self.records[message.id] = Record(message.id, message.owner, message.epoch)
            self._log(record)
            return APPLIED
        if message.owner in record.barred:
            return INVALID_OWNER
        if message.epoch > record.epoch:
            self._update(record, message.owner, message.epoch)
            return APPLIED
        if message.epoch < record.epoch:
            return STALE_EPOCH
        if record.owner == message.owner:
            return DUPLICATE
        if record.owner == NO_OWNER or message.owner > record.owner:
            return STALE_EPOCH
        self._update(record, message.owner, message.epoch)  # two claims of one epoch: the lower slot wins everywhere
        return APPLIED

    def _decline(self, sender, message):
        record = self.records.get(message.id)
        if record is None:
            return UNKNOWN_OBJECT
        if message.epoch != record.epoch:
            return STALE_EPOCH
        if sender != message.owner or record.owner != message.owner:
            return WRONG_SENDER
        record.barred.add(message.owner)
        self._update(record, NO_OWNER, record.epoch + 1)
        return APPLIED

    def _stop(self, sender, message):
        record = self.records.get(message.id)
        if record is None:
            return UNKNOWN_OBJECT
        if message.epoch != record.epoch:
            return STALE_EPOCH
        if record.owner != message.owner or (sender != record.owner and sender != self.host):
            return WRONG_SENDER
        if sender == record.owner:
            record.barred.clear()  # a voluntary release gives everyone a fresh start
        self._update(record, NO_OWNER, record.epoch + 1)
        return APPLIED

    def _update(self, record, owner, epoch):
        record.owner, record.epoch = owner, epoch
        self._log(record)

    def _log(self, record):
        self.changes.append((time.time() * 1000.0, record.id, record.owner, record.epoch))

    def drop_slot(self, slot):
        """The slot left: every record it owned becomes unowned (one epoch up) and it is no longer barred anywhere."""
        for record in self.records.values():
            record.barred.discard(slot)
            if record.owner == slot:
                self._update(record, NO_OWNER, record.epoch + 1)

    def owner_of(self, scene):
        record = self.records.get(scene)
        return record.owner if record else NO_OWNER

    def epoch_of(self, scene):
        record = self.records.get(scene)
        return record.epoch if record else 0

    def claims_of(self, slot):
        return [Auth(r.id, r.epoch, slot) for r in self.records.values() if r.owner == slot]


class RoomAuthority(Module):
    HANDLES = {CLAIM: "on_auth", DECLINE: "on_auth", STOP: "on_auth"}

    def __init__(self, ctx):
        super().__init__(ctx)
        self.table = AuthorityTable()
        self.mode = "normal"
        self.claim_delay_s = DEFAULT_CLAIM_DELAY_S
        self.refused = []  # (kind, scene, outcome) of messages the rules refused
        self._arrived = {}  # scene -> when the fake's character entered it
        self._next_reconcile = 0.0
        self._peer_was_up = False

    def _sync_session(self):
        s = self.ctx.link.session
        if s.fake_slot is not None and self.table.local != s.fake_slot:
            self.table.set_session(s.fake_slot, s.host_slot)

    def on_auth(self, frame):
        self._sync_session()
        message = wire.AuthMessage.unpack(frame.payload)
        outcome = self.table.on_message(frame.type, frame.slot, Auth(message.id, message.epoch, message.owner))
        if outcome not in (APPLIED, DUPLICATE):
            self.refused.append((wire.type_name(frame.type), message.id, outcome))

    def apply_extras(self, claims, sender):
        """The claims a JOIN_EXTRAS carries (the sender's records), as if it had claimed them."""
        self._sync_session()
        for claim in claims:
            self.table.on_message(CLAIM, sender, Auth(claim.id, claim.epoch, claim.owner))

    def claims_of(self, slot):
        return self.table.claims_of(slot)

    def _send(self, outgoing):
        if outgoing:
            kind, message = outgoing
            self.ctx.link.send(kind, wire.AuthMessage.pack(id=message.id, epoch=message.epoch, owner=message.owner))

    def entered(self, scene):
        """The fake's character entered `scene`; `tie` mode claims at once, before the game's claim can be heard."""
        self._arrived[scene] = time.monotonic()
        self._sync_session()
        if self.mode == "tie":
            self._send(self.table.claim(scene))

    def claim_scene(self, scene):
        """Claims a scene the fake has already moved to ('first': before the game loads it)."""
        self._sync_session()
        self._send(self.table.claim(scene))

    def force_claim(self, scene):
        """A claim with the next epoch over the game's owned room: legal on the wire, never sent by a real adapter."""
        self._sync_session()
        message = Auth(scene, self.table.epoch_of(scene) + 1, self.table.local)
        self.table.on_message(CLAIM, self.table.local, message)
        self._send((CLAIM, message))

    def decline(self, scene):
        self._send(self.table.decline(scene))

    def release_scene(self, scene):
        """The fake gives up a room it owns (ROOM_STOP), as it does when it walks out of it."""
        self._sync_session()
        self._send(self.table.release(scene))

    def reconcile(self):
        """room_authority_rule::reconcile: release rooms the fake owns but left, claim the loaded room when unowned."""
        self._sync_session()
        here = self.ctx.peer.place.scene
        for owned in self.table.claims_of(self.table.local):
            if owned.id != here:
                self._send(self.table.release(owned.id))
        due = time.monotonic() - self._arrived.get(here, 0.0) >= self.claim_delay_s
        if here != wire.NO_SCENE and due and self.table.owner_of(here) == NO_OWNER:
            self._send(self.table.claim(here))

    def tick(self, now):
        s = self.ctx.link.session
        up = s.peer_up.is_set()
        if up and not self._peer_was_up:
            self._sync_session()
            for claim in self.table.claims_of(self.table.local):
                self._send((CLAIM, claim))
        self._peer_was_up = up
        if now >= self._next_reconcile and up and self.mode != "first":
            self._next_reconcile = now + RECONCILE_PERIOD_S
            self.reconcile()
