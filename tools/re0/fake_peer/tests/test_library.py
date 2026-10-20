"""The journal file, the library built from recorded frames, and the launcher / game folder lookups."""
import json
import unittest

from fake_peer import gamedir, launchers, wire
from fake_peer.library import IN, OUT, Entry, Journal, Library, build, read_journal
from fake_peer.tests.support import scratch_path
from fake_peer.wire import Frame, Msg

SCENE, TARGET = 0x10, 0x11


def frame_entry(t, msg_type, payload, direction=IN):
    return Entry(t, direction, int(msg_type), 0, wire.FLAG_RELIABLE, payload)


class JournalTests(unittest.TestCase):
    def test_frames_and_notes_survive_the_file(self):
        path = scratch_path() / "frames.jsonl"
        journal = Journal(path)
        journal.frame(OUT, Frame(Msg.POSE_STATE, 0, 1, b"\x01\x02"), t=5.0)
        journal.note("mark clip")
        journal.close()
        entries = read_journal(path)
        self.assertEqual((entries[0].type, entries[0].payload, entries[0].dir), (Msg.POSE_STATE, b"\x01\x02", OUT))
        self.assertEqual(entries[1].note, "mark clip")

    def test_filters(self):
        journal = Journal()
        journal.frame(IN, Frame(Msg.POSE_STATE, 0, 1, b""), t=1.0)
        journal.frame(OUT, Frame(Msg.POSE_STATE, 0, 1, b""), t=2.0)
        self.assertEqual(len(journal.frames(Msg.POSE_STATE, IN)), 1)
        self.assertEqual(len(journal.frames(None, None, since=1.5)), 1)


class LibraryTests(unittest.TestCase):
    def test_a_door_is_paired_with_the_arrival_pose(self):
        follow = wire.DoorFollow.pack(serial=1, source_scene=SCENE, scene=TARGET, entry=2)
        pose = wire.PoseState.pack(room=TARGET, pos=(5.0, 6.0, 7.0), load_serial=1)
        library = Library()
        library.ingest("run1", [frame_entry(1, Msg.DOOR_FOLLOW, follow), frame_entry(2, Msg.POSE_STATE, pose)])
        door = library.door(SCENE)
        self.assertEqual((door["follow"]["entry"], door["arrival"], door["run"]), (2, (5.0, 6.0, 7.0), "run1"))
        self.assertIsNone(library.door(TARGET))

    def test_a_pose_clip_is_cut_between_its_marks(self):
        pose = wire.PoseState.pack(room=SCENE, load_serial=1)
        entries = [Entry(1, note="mark clip:billy:run:start"), frame_entry(2, Msg.POSE_STATE, pose),
                   frame_entry(3, Msg.MOTION_EVENT, wire.MotionEvent.pack()), Entry(4, note="mark clip:billy:run:end"),
                   frame_entry(5, Msg.POSE_STATE, pose)]
        library = Library()
        library.ingest("run1", entries)
        clip = library.pose_clip("billy")
        self.assertEqual([f["type"] for f in clip["frames"]], [Msg.POSE_STATE, Msg.MOTION_EVENT])
        self.assertEqual(clip["frames"][0]["dt"], 1.0)

    def test_an_enemy_clip_is_keyed_by_scene_and_the_games_load(self):
        pose = wire.PoseState.pack(room=SCENE, load_serial=4)
        batch = wire.pack_enemy_batch(dict(scene=SCENE, seq=1), wire.EnemyPoseEntry, [dict(slot=1)])
        library = Library()
        library.ingest("run1", [frame_entry(1, Msg.POSE_STATE, pose), frame_entry(2, Msg.ENEMY_POSE, batch)])
        self.assertIn((SCENE, 4), library.enemy_clips)
        self.assertEqual(library.enemy_clip(SCENE)["run"], "run1")

    def test_single_item_kinds(self):
        vtable = library_vtable()
        hit = wire.HitRequest.pack(vtable=vtable, attack_type=3)
        lease = wire.EnemyLease.pack(token=(1 << 16) | 1, class_index=5, hp=10)
        put = wire.FloorPut.pack(room=SCENE, item_id=4, count=2, pos=(1.0, 2.0, 3.0))
        move = wire.PropMove.pack(scene=SCENE, kind=wire.PROP_OBSTACLE_KIND, unit=1, pos=(9.0, 8.0, 7.0), yaw=0.5)
        library = Library()
        library.ingest("run1", [frame_entry(1, Msg.HIT_REQUEST, hit), frame_entry(2, Msg.ENEMY_LEASE, lease),
                                frame_entry(3, Msg.FLOOR_PUT, put), frame_entry(4, Msg.PROP_MOVE, move),
                                frame_entry(5, Msg.HIT_REQUEST, hit, OUT)])
        self.assertIsNotNone(library.hit_request(0))
        self.assertIsNotNone(library.lease_grant(5))
        self.assertEqual(library.items_in(SCENE)[0]["item_id"], 4)
        self.assertEqual(library.box(SCENE, 1)["pos"], (9.0, 8.0, 7.0))
        self.assertTrue(library.has("box"))
        self.assertFalse(Library().has("door"))

    def test_build_reads_every_run_folder_and_writes_the_index(self):
        root = scratch_path()
        run = root / "20261020-100000-t9_rooms"
        run.mkdir()
        journal = Journal(run / "frames.jsonl")
        journal.frame(IN, Frame(Msg.FLOOR_TAKE, 1, 0, wire.FloorTake.pack(room=SCENE, item_id=6)))
        journal.close()
        library = build(root)
        self.assertEqual(library.items_in(SCENE)[0]["run"], run.name)
        index = json.loads((root / "library.json").read_text(encoding="utf-8"))
        self.assertEqual(len(index["floor_items"]), 1)


def library_vtable():
    from fake_peer import wire_check
    return wire_check.vtables("kEnemyVtables")[0]


class FolderTests(unittest.TestCase):
    def test_the_library_folder_that_lists_the_app_is_found(self):
        vdf = '"libraryfolders"\n{\n"0"\n{\n"path"\t\t"C:\\\\Steam"\n"apps"\n{\n"1"\t\t"2"\n}\n}\n"1"\n{\n"path"\t\t"D:\\\\Lib"\n"apps"\n{\n"339340"\t\t"9"\n}\n}\n}'
        self.assertEqual(str(gamedir.library_of_app(vdf)), "D:\\Lib")
        self.assertIsNone(gamedir.library_of_app(vdf, "5"))

    def test_launcher_commands_keep_the_game_off_the_fake_bridge(self):
        ports = launchers.Ports()
        host_seat = launchers.Launchers("coop-launcher.exe", scratch_path(), ports, fake_seat="host").commands()
        guest_seat = launchers.Launchers("coop-launcher.exe", scratch_path(), ports, fake_seat="guest").commands()
        self.assertIn(str(ports.fake_bridge), host_seat["host"])
        self.assertNotIn("--bridge-port", guest_seat["host"])
        self.assertIn(str(ports.fake_bridge), guest_seat["join"])
        self.assertNotIn(str(27960), sum(guest_seat.values(), []))


if __name__ == "__main__":
    unittest.main()
