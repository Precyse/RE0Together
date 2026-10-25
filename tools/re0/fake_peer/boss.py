"""Boss hand-over (BOSS_HANDOVER 0x0169), both seats.

New authority seat (the game runs the boss room and starts a door): the fake checks a Prepare against the enemy stream it
observed, answers Ready (or Cancel, or nothing), and on Commit waits for the game to leave the room, claims it and holds the
members still (it has no boss AI, which is enough for the transaction), sending Outcome when a member dies.
Old authority seat (the fake runs the boss room by replay): `door` freezes its replay, offers the members of its last batch,
commits on Ready and leaves the room."""
import time

from fake_peer import wire
from fake_peer import wire_check
from fake_peer.link import Module
from fake_peer.wire import Msg

TOKEN_SLOT_SHIFT = 24
TOKEN_COUNTER_MASK = (1 << TOKEN_SLOT_SHIFT) - 1
HOLD_HZ = 20.0
PREPARE_TIMEOUT_S = 5.0
ANSWERS = ("ready", "cancel", "none")
REASON_MEMBER = 4  # boss_wire::Reason::Member


def boss_classes():
    enemy, boss = wire_check.vtables("kEnemyVtables"), wire_check.vtables("kBossVtables")
    return {enemy.index(v) for v in boss}


class Boss(Module):
    HANDLES = {Msg.BOSS_HANDOVER: "on_message"}

    def __init__(self, ctx):
        super().__init__(ctx)
        self.answer = "ready"
        self.cancel_reason = REASON_MEMBER
        self.leave_mid_to = None  # a scene to walk to as soon as a Prepare is answered
        self.classes = boss_classes()
        self.log = []  # (phase name, token, alive) of every boss message seen
        self.new = None   # {"token", "scene", "members", "alive", "stage"}
        self.old = None   # {"token", "scene", "members", "stage", "since"}
        self._counter = 0
        self._next_hold = 0.0

    # ---- both seats ----------------------------------------------------------------------------------------------

    def on_message(self, frame):
        header, members = wire.unpack_boss(frame.payload)
        self.log.append((header.phase, header.token, header.alive))
        if header.phase == wire.BOSS_PREPARE:
            self._on_prepare(header, members)
        elif header.phase == wire.BOSS_READY:
            self._on_ready(header)
        elif header.phase == wire.BOSS_COMMIT and self.new and self.new["token"] == header.token:
            self.new["stage"] = "committed"
        elif header.phase == wire.BOSS_CANCEL:
            self._on_cancel(header)

    def _send(self, token, scene, phase, alive=0, reason=0, members=()):
        epoch = self.ctx.authority.table.epoch_of(scene)
        payload = wire.pack_boss(dict(token=token, room_epoch=epoch, scene=scene, phase=phase, alive=alive, reason=reason),
                                 list(members))
        self.ctx.link.send(Msg.BOSS_HANDOVER, payload)

    # ---- new authority seat --------------------------------------------------------------------------------------

    def _fits(self, member):
        seen = self.ctx.enemies.observed.get(member.slot)
        return seen is not None and (seen.class_index, seen.spawn_id, seen.spawn_record) == (
            member.class_index, member.spawn_id, member.spawn_record)

    def _on_prepare(self, header, members):
        if self.answer == "none":
            return
        if self.answer == "cancel" or not all(self._fits(m) for m in members):
            self._send(header.token, header.scene, wire.BOSS_CANCEL, reason=self.cancel_reason)
            return
        self.new = dict(token=header.token, scene=header.scene, members=[m._asdict() for m in members], alive=header.alive,
                        stage="prepared")
        self._send(header.token, header.scene, wire.BOSS_READY)
        if self.leave_mid_to is not None:  # leaving while the Prepare is open: the game must cancel it as 'left'
            self.ctx.rooms.move(self.leave_mid_to)
            self.leave_mid_to = None

    def kill(self, index=0):
        """Member `index` of the transaction dies here: terminal sent once, reliably, then the Outcome with the new mask."""
        run = self.new
        if not run or run["stage"] != "running":
            return False
        member = run["members"][index]
        member["hp"] = -1
        run["alive"] &= ~(1 << index)
        self.ctx.enemies.send_batch(run["scene"], [self._entry(member, dead=True)], reliable=True)
        self._send(run["token"], run["scene"], wire.BOSS_OUTCOME, alive=run["alive"])
        return True

    def _entry(self, member, dead=False):
        flags = wire.ENEMY_TERMINAL if dead else wire.ENEMY_ALIVE
        return dict(slot=member["slot"], class_index=member["class_index"], flags=flags, target=wire.NO_TARGET,
                    spawn_id=member["spawn_id"], spawn_record=member["spawn_record"], hp=member["hp"], pos=member["pos"], quat=member["quat"],
                    motion0=wire.NO_MOTION, motion1=wire.NO_MOTION, record=member["record"])

    def _tick_new(self, now):
        run = self.new
        if not run or run["stage"] not in ("committed", "running"):
            return
        game_room = self.ctx.remote.pose.room if self.ctx.remote.pose else run["scene"]
        if run["stage"] == "committed":
            if game_room == run["scene"]:
                return
            self.ctx.rooms.move(run["scene"])
            self.ctx.authority.claim_scene(run["scene"])
            if self.ctx.authority.table.owner_of(run["scene"]) == self.ctx.authority.table.local:
                run["stage"] = "running"
        elif now >= self._next_hold:
            self._next_hold = now + 1.0 / HOLD_HZ
            alive = [m for i, m in enumerate(run["members"]) if run["alive"] & (1 << i)]
            if alive:
                self.ctx.enemies.send_batch(run["scene"], [self._entry(m) for m in alive])

    # ---- old authority seat --------------------------------------------------------------------------------------

    def door(self, leave_to=None):
        """Freezes the replay, offers the members of its last batch and (on Ready) commits and leaves the room."""
        scene = self.ctx.peer.place.scene
        members = self._members_of_replay()
        if not members:
            return False
        self.ctx.enemies.pause_replay(True)
        self._counter = (self._counter + 1) & TOKEN_COUNTER_MASK
        token = ((self.ctx.peer.slot + 1) << TOKEN_SLOT_SHIFT) | self._counter
        alive = (1 << len(members)) - 1
        self.old = dict(token=token, scene=scene, members=members, stage="preparing", since=time.monotonic(), leave_to=leave_to)
        self._send(token, scene, wire.BOSS_PREPARE, alive=alive, members=members)
        return True

    def _members_of_replay(self):
        members = []
        for entry in self.ctx.enemies.replay_entries():
            if entry.class_index in self.classes and len(members) < wire.MAX_BOSS_MEMBERS:
                members.append(dict(slot=entry.slot, class_index=entry.class_index, spawn_id=entry.spawn_id,
                                    spawn_record=entry.spawn_record, hp=entry.hp, pos=entry.pos, quat=entry.quat,
                                    record=entry.record))
        return members

    def _on_ready(self, header):
        old = self.old
        if not old or old["token"] != header.token or old["stage"] != "preparing":
            return
        old["stage"] = "committed"
        self._send(old["token"], old["scene"], wire.BOSS_COMMIT)
        target = old["leave_to"] or next((s for s in sorted(self.ctx.rooms.known) if s != old["scene"]), None)
        if target is not None:
            self.ctx.rooms.move(target)

    def _on_cancel(self, header):
        if self.old and self.old["token"] == header.token:
            self.old["stage"] = "cancelled"
            self.ctx.enemies.pause_replay(False)
        if self.new and self.new["token"] == header.token:
            self.new = None

    def tick(self, now):
        self._tick_new(now)
        old = self.old
        if old and old["stage"] == "preparing" and time.monotonic() - old["since"] > PREPARE_TIMEOUT_S:
            old["stage"] = "cancelled"
            self.ctx.enemies.pause_replay(False)
