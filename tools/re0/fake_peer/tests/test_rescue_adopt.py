"""The pieces of the rescue and adopt scenarios: the right-record-under-another-slot probe, the final round of a leaving
simulator, the dev command file and the gates of t6_authority (adopt) and t9_rooms (rescue) on synthetic logs and frames."""
import tempfile
import unittest
from pathlib import Path

from fake_peer import wire
from fake_peer.gates import FAIL, MISSING, PASS, Evidence
from fake_peer.library import Journal
from fake_peer.logwatch import LogStore
from fake_peer.scenarios import t6_authority, t9_rooms
from fake_peer.tests.support import deliver, make_context, pose_from_game, tick
from fake_peer.tests.test_enemy_modules import SCENE, batch, entry
from fake_peer.wire import Msg

SCENE_B = 0x2E
SLOT, OTHER_SLOT = 4, 5
DEAD_HP = -1


def replay_context():
    ctx = make_context("guest")
    pose_from_game(ctx, SCENE)
    ctx.rooms.move(SCENE)
    ctx.authority.claim_scene(SCENE)
    clip = {"frames": [dict(dt=0.0, type=Msg.ENEMY_POSE, payload=batch([entry(SLOT), entry(OTHER_SLOT, spawn_id=8, spawn_record=3)], epoch=1))]}
    ctx.enemies.start_replay(SCENE, clip)
    tick(ctx, 0.1)
    ctx.link.sent.clear()
    return ctx


def pose_entries(ctx):
    return [e for f in ctx.link.of(Msg.ENEMY_POSE) for e in wire.unpack_enemy_batch(wire.EnemyPoseEntry, f.payload)[1]]


class SlotProbeTests(unittest.TestCase):
    def test_the_slot_probe_keeps_the_record_and_moves_the_pool_slot(self):
        ctx = replay_context()
        ctx.enemies.pause_replay()
        self.assertTrue(ctx.enemies.probe("slot"))
        sent = pose_entries(ctx)[0]
        self.assertEqual((sent.spawn_record, sent.spawn_id, sent.slot), (2, 7, SLOT + 1))

    def test_the_slot_probe_wraps_at_the_end_of_the_pool(self):
        ctx = replay_context()
        ctx.enemies.pause_replay()
        ctx.enemies.start_replay(SCENE, {"frames": [dict(dt=0.0, type=Msg.ENEMY_POSE, payload=batch([entry(36)], epoch=1))]})
        ctx.enemies.pause_replay()
        ctx.enemies.probe("slot")
        self.assertEqual(pose_entries(ctx)[0].slot, 0)


class FinalRoundTests(unittest.TestCase):
    def test_the_final_round_is_one_reliable_batch_of_every_enemy_as_it_stands(self):
        ctx = replay_context()
        ctx.enemies._replay_slots[OTHER_SLOT].hp = DEAD_HP
        self.assertTrue(ctx.enemies.send_final(SCENE))
        frames = ctx.link.of(Msg.ENEMY_POSE)
        self.assertEqual(len(frames), 1)
        self.assertTrue(frames[0].flags & wire.FLAG_RELIABLE)
        shown = {e.slot: e for e in pose_entries(ctx)}
        self.assertEqual((shown[SLOT].flags, shown[SLOT].hp > 0), (wire.ENEMY_ALIVE, True))
        self.assertEqual((shown[OTHER_SLOT].flags, shown[OTHER_SLOT].hp), (wire.ENEMY_TERMINAL, DEAD_HP))

    def test_nothing_replayed_sends_nothing(self):
        ctx = make_context("guest")
        self.assertFalse(ctx.enemies.send_final(SCENE))
        self.assertEqual(ctx.link.of(Msg.ENEMY_POSE), [])

    def test_the_fake_gives_a_room_up_with_a_stop(self):
        ctx = replay_context()
        ctx.link.sent.clear()
        ctx.authority.release_scene(SCENE)
        self.assertEqual([f.type for f in ctx.link.sent], [Msg.ROOM_STOP])
        self.assertEqual(ctx.authority.table.owner_of(SCENE), 255)


class DevCommandTests(unittest.TestCase):
    def context_for(self, game_dir, dev_commands):
        ctx = make_context("guest")
        ctx.options.game_dir, ctx.options.dev_commands, ctx.options.no_game = str(game_dir), dev_commands, False
        return ctx

    def test_a_run_without_dev_commands_writes_nothing(self):
        with tempfile.TemporaryDirectory() as folder:
            (Path(folder) / "coop").mkdir()
            self.assertFalse(self.context_for(folder, False).dev_command("rescue"))
            self.assertFalse((Path(folder) / "coop" / "cmd.txt").exists())

    def test_a_run_with_dev_commands_writes_the_command_line(self):
        with tempfile.TemporaryDirectory() as folder:
            (Path(folder) / "coop").mkdir()
            self.assertTrue(self.context_for(folder, True).dev_command("rescue"))
            self.assertEqual((Path(folder) / "coop" / "cmd.txt").read_text(encoding="utf-8"), "rescue\n")


def evidence(lines, frames=(), marks=None):
    store = LogStore()
    for t, text in lines:
        store.add("adapter", text, t)
    journal = Journal()
    for t, payload in frames:
        journal.frame("in", wire.Frame(int(Msg.ROOM_MOVE), wire.FLAG_RELIABLE, 0, payload), t=t)
    return Evidence(store, journal, marks or {})


ADOPTED = "enemy_observer: authority -> here (scene 0x2d e3), {n} enemies resumed (2 at step 0, adopted {a})"
RELOAD = "rescue: reloading scene 0x2d through entry 1"


def room_move(scene):
    return wire.RoomMove.pack(seq=1, scene=scene, entry=1, load_serial=2, mode=wire.AUTO_SPOT)


class AdoptGateTests(unittest.TestCase):
    def verdict(self, lines):
        return t6_authority.adopted_all(evidence(lines, marks={"simulator-leaves": 1000})).verdict

    def test_every_resumed_enemy_adopted_passes(self):
        self.assertEqual(self.verdict([(2000, ADOPTED.format(n=3, a=3))]), PASS)

    def test_an_enemy_resumed_from_its_own_state_fails(self):
        self.assertEqual(self.verdict([(2000, ADOPTED.format(n=3, a=2))]), FAIL)

    def test_no_enemy_at_all_fails_and_no_line_is_missing(self):
        self.assertEqual(self.verdict([(2000, ADOPTED.format(n=0, a=0))]), FAIL)
        self.assertEqual(self.verdict([(2000, "enemy_observer: showing scene 0x2d from slot 1 e3")]), MISSING)

    def test_a_line_before_the_fake_left_does_not_count(self):
        self.assertEqual(self.verdict([(500, ADOPTED.format(n=3, a=3))]), MISSING)


class RescueGateTests(unittest.TestCase):
    def verdict(self, lines, frames):
        return t9_rooms.reload_sends_room_move(evidence(lines, frames, {"rescue": 1000})).verdict

    def test_a_room_move_to_the_reloaded_scene_after_the_line_passes(self):
        self.assertEqual(self.verdict([(2000, RELOAD)], [(2100, room_move(SCENE))]), PASS)

    def test_a_room_move_to_another_scene_or_before_the_line_fails(self):
        self.assertEqual(self.verdict([(2000, RELOAD)], [(2100, room_move(SCENE_B))]), FAIL)
        self.assertEqual(self.verdict([(2000, RELOAD)], [(1500, room_move(SCENE))]), FAIL)

    def test_no_reload_line_is_missing(self):
        self.assertEqual(self.verdict([(2000, "rescue: reload refused (boss alive)")], [(2100, room_move(SCENE))]), MISSING)


if __name__ == "__main__":
    unittest.main()
