"""The rules of the echo hub without any I/O (docs/SR2_NOTES.md "Echo testing"): a direct port of launcher/SlotAssigner.cs and
launcher/Session.cs for N instances that share one lobby, plus the relay decision for a GAME_DATAGRAM. Instances are numbered
1..N and have fixed, ascending fake SteamIDs, so instance 1 (the lobby owner) is slot 0 and instance k is slot k-1 while
everybody is present. Each instance has its own View, exactly as each real launcher has its own Session; the frames a view
produces are returned as Welcome / PeerUp / PeerDown values and the caller encodes them with wire.py."""
from collections import Counter
from dataclasses import dataclass
from typing import NamedTuple

import wire

HOST_SLOT = 0
OWNER = 1
BASE_STEAM_ID = 76561198000001000
FIRST_EPOCH = 1
MAX_INSTANCES = 4
PEER_NAME = "Echo {}"

NO_SLOT = "no_slot"
NO_MEMBER = "no_member"
NOT_LINKED = "not_linked"
DROPPED_LINK = "dropped_link"
DROP_REASONS = (NO_SLOT, NO_MEMBER, NOT_LINKED, DROPPED_LINK)


class Welcome(NamedTuple):
    local_slot: int
    host_slot: int
    max_players: int
    epoch: int
    local_id: int


class PeerUp(NamedTuple):
    slot: int
    steam_id: int
    name: str


class PeerDown(NamedTuple):
    slot: int


class Route(NamedTuple):
    """Where one GAME_DATAGRAM goes: (receiver instance, slot to write) pairs, and the reason for every copy not delivered."""
    deliveries: tuple
    drops: tuple


def steam_id(instance):
    return BASE_STEAM_ID + instance


def instance_of(steam_id_value):
    return steam_id_value - BASE_STEAM_ID


def assert_distinct_addresses(count):
    """Two members with one virtual address leave the higher SteamID unmapped in the tunnel, so the fake IDs must not collide."""
    seen = {}
    for instance in range(1, count + 1):
        address = wire.virtual_address(steam_id(instance))
        if address in seen:
            raise ValueError(f"instances {seen[address]} and {instance} share the virtual address {address}")
        seen[address] = instance


def assign_slots(host_id, member_ids, max_players):
    """SlotAssigner.Assign: the host gets slot 0, the rest sorted ascending by id fill 1..max_players-1, extras get no slot."""
    slots = {host_id: HOST_SLOT}
    next_slot = HOST_SLOT + 1
    for member_id in sorted(set(member_ids) - {host_id}):
        if next_slot >= max_players:
            break
        slots[member_id] = next_slot
        next_slot += 1
    return slots


def snapshot_events(view_id, slots, epoch, max_players, names):
    """Session.SendSnapshot: WELCOME, then one PEER_UP per other member."""
    events = [Welcome(slots[view_id], HOST_SLOT, max_players, epoch, view_id)]
    events += [PeerUp(slot, member_id, names[member_id]) for member_id, slot in slots.items() if member_id != view_id]
    return events


class View:
    """One launcher's Session: its slot map, slot and epoch, and what it tells its game when the lobby changes."""

    def __init__(self, instance, max_players):
        self.instance = instance
        self.id = steam_id(instance)
        self.max_players = max_players
        self.ready = False
        self.active = False
        self.ended = False
        self.host_id = 0
        self.slots = {}
        self.local_slot = HOST_SLOT
        self.epoch = 0

    def peers(self):
        return {slot: member_id for member_id, slot in self.slots.items() if member_id != self.id}

    def on_ready(self, names):
        """OnAdapterReady: the game said HELLO."""
        self.ready = True
        return snapshot_events(self.id, self.slots, self.epoch, self.max_players, names) if self.active and not self.ended else []

    def on_unlinked(self):
        self.ready = False

    def apply(self, members, owner_id, names):
        """ApplyMembership for the lobby's members (SteamIDs) and owner."""
        if self.ended:
            return []
        owner_present = owner_id in members
        if not self.active and not owner_present:
            return []
        if self.active and (not owner_present or owner_id != self.host_id):
            return self.end()
        host_id = self.host_id if self.active else owner_id
        slots = assign_slots(host_id, members, self.max_players)
        if self.id not in slots:
            return self.end()
        if not self.active:
            self.active = True
            self.host_id = host_id
            return self.reset(slots, slots[self.id], FIRST_EPOCH, names)
        if slots[self.id] != self.local_slot:
            return self.reset(slots, slots[self.id], self.epoch + 1, names)
        return self.update_peers(slots, names)

    def reset(self, slots, local_slot, epoch, names):
        self.slots, self.local_slot, self.epoch = slots, local_slot, epoch
        return snapshot_events(self.id, slots, epoch, self.max_players, names) if self.ready else []

    def update_peers(self, slots, names):
        previous, self.slots = self.slots, slots
        events = [PeerDown(slot) for member_id, slot in previous.items()
                  if member_id != self.id and slots.get(member_id) != slot]
        events += [PeerUp(slot, member_id, names[member_id]) for member_id, slot in slots.items()
                   if member_id != self.id and previous.get(member_id) != slot]
        return events if self.ready else []

    def end(self):
        self.epoch += 1
        self.ended = True
        return [PeerDown(slot) for member_id, slot in self.slots.items() if member_id != self.id] if self.ready else []


@dataclass
class Traffic:
    sent: int = 0
    received: int = 0


class Views:
    """The shared lobby of N instances, one View each, the link faults and the relay counters."""

    def __init__(self, count, max_players):
        if not 1 <= count <= MAX_INSTANCES:
            raise ValueError(f"{count} instances, expected 1 to {MAX_INSTANCES}")
        assert_distinct_addresses(count)
        self.count = count
        self.max_players = max_players
        self.names = {steam_id(k): PEER_NAME.format(k) for k in range(1, count + 1)}
        self.lobby = set(range(1, count + 1))
        self.views = {k: View(k, max_players) for k in self.lobby}
        self.down = set()
        self.drops = Counter()
        self.traffic = {k: Traffic() for k in self.lobby}
        self.apply()

    def apply(self):
        """Every view applies the lobby, in instance order; the frames for each instance whose game is linked."""
        members = {steam_id(k) for k in self.lobby}
        return {k: view.apply(members, steam_id(OWNER), self.names) for k, view in self.views.items()}

    def hello(self, instance):
        return self.views[instance].on_ready(self.names)

    def unlinked(self, instance):
        self.views[instance].on_unlinked()

    def leave(self, instance):
        """The instance leaves the lobby: the frames every view sends. A host leaving ends every session."""
        self.lobby.discard(instance)
        return self.apply()

    def back(self, instance):
        """The instance rejoins: its launcher starts a new session (epoch 1). After a host exit every ended session restarts."""
        self.lobby.add(instance)
        if OWNER in self.lobby:
            for k in self.lobby:
                if self.views[k].ended:
                    ready = self.views[k].ready
                    self.views[k] = View(k, self.max_players)
                    self.views[k].ready = ready
        return self.apply()

    def drop_link(self, instance):
        self.down.add(instance)

    def restore_link(self, instance):
        self.down.discard(instance)

    def slot_of(self, instance):
        view = self.views[instance]
        return view.local_slot if view.active and not view.ended else None

    def last_slot(self, instance):
        """The slot the instance had when it was last in a session, also after it left or was kicked."""
        view = self.views[instance]
        return view.local_slot if view.active else instance - 1

    def route(self, sender, dst_slot):
        """Where a datagram the sender's game wrote for dst_slot (SLOT_ALL = every other member) goes, counting every drop."""
        view = self.views[sender]
        if not view.active or view.ended:
            return self.dropped([NO_SLOT])
        if sender in self.down:
            return self.dropped([DROPPED_LINK])
        if dst_slot == wire.SLOT_ALL:
            targets = [instance_of(member_id) for member_id in view.peers().values()]
        elif dst_slot in view.peers():
            targets = [instance_of(view.peers()[dst_slot])]
        else:
            return self.dropped([NO_MEMBER])
        deliveries, reasons = [], []
        for target in targets:
            receiver = self.views[target]
            slot = receiver.slots.get(view.id)
            if target in self.down:
                reasons.append(DROPPED_LINK)
            elif not receiver.ready:
                reasons.append(NOT_LINKED)
            elif not receiver.active or receiver.ended or slot is None:
                reasons.append(NO_SLOT)
            else:
                deliveries.append((target, slot))
        self.traffic[sender].sent += len(deliveries)
        for target, _slot in deliveries:
            self.traffic[target].received += 1
        return self.dropped(reasons, deliveries)

    def dropped(self, reasons, deliveries=()):
        self.drops.update(reasons)
        return Route(tuple(deliveries), tuple(reasons))

    def clear_counters(self):
        self.drops.clear()
