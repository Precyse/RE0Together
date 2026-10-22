"""Pose sources, the send guard, rooms and doors, with the real modules on a recording link."""
import struct
import unittest

from fake_peer import wire
from fake_peer.library import Library
from fake_peer.rooms import UnknownScene
from fake_peer.tests.support import deliver, make_context, pose_from_game, tick
from fake_peer.wire import Msg

SCENE, OTHER = 0x2D, 0x2E


class EchoTests(unittest.TestCase):
    def setUp(self):
        self.ctx = make_context("guest")
        self.link = self.ctx.link

    def test_a_pose_comes_back_beside_the_sender_in_the_games_room_with_the_fakes_clock(self):
        pose_from_game(self.ctx, SCENE, pos=(100.0, 5.0, 6.0), load_serial=9, seq=77)
        self.ctx.sources.use("echo", delay=0.0, offset_x=150.0)
        pose_from_game(self.ctx, SCENE, pos=(100.0, 5.0, 6.0), load_serial=9, seq=78)
        tick(self.ctx)
        move = wire.RoomMove.unpack(self.link.of(Msg.ROOM_MOVE)[0].payload)
        echoed = wire.PoseState.unpack(self.link.of(Msg.POSE_STATE)[0].payload)
        self.assertEqual((move.scene, move.character), (SCENE, wire.BILLY))
        self.assertEqual((echoed.pos, echoed.room, echoed.load_serial, echoed.seq), ((250.0, 5.0, 6.0), SCENE, self.ctx.peer.place.load_serial, 1))
        self.assertNotEqual(echoed.load_serial, 9)  # the fake's own load serial, not the game's

    def test_nothing_is_echoed_before_its_delay(self):
        pose_from_game(self.ctx, SCENE)
        self.ctx.sources.use("echo", delay=60.0)
        pose_from_game(self.ctx, SCENE)
        tick(self.ctx)
        self.assertEqual(self.link.of(Msg.POSE_STATE), [])

    def test_weapon_fx_gets_the_fakes_character_clock_and_position(self):
        pose_from_game(self.ctx, SCENE)
        self.ctx.sources.use("echo", delay=0.0, offset_x=150.0)
        header = wire.WeaponFxHeader.pack(sender_ms=1, seq=40, character=wire.REBECCA, count=1)
        deliver(self.ctx, Msg.WEAPON_FX, header + struct.pack("<BBBB3f", 5, 30, 0, 0, 10.0, 0.0, 0.0))
        tick(self.ctx)
        out = self.link.of(Msg.WEAPON_FX)[0].payload
        head = wire.WeaponFxHeader.unpack(out[:wire.WeaponFxHeader.size])
        self.assertEqual((head.character, head.seq), (wire.BILLY, 1))
        self.assertEqual(struct.unpack_from("<f", out, wire.WeaponFxHeader.size + 4)[0], 160.0)

    def test_the_input_echo_repeats_pad_and_player_state_untouched_except_x(self):
        self.ctx.sources.use("echo", types="input", delay=0.0, offset_x=0.0)
        state = wire.PlayerState.pack(seq=3, pos=(1.0, 2.0, 3.0))
        deliver(self.ctx, Msg.PLAYER_STATE, state, flags=0)
        deliver(self.ctx, Msg.PAD_FRAME, b"pad")
        tick(self.ctx)
        self.assertEqual([f.payload for f in self.link.sent], [state, b"pad"])

    def test_stand_replay_held_and_silent(self):
        self.ctx.rooms.known.add(SCENE)
        self.ctx.rooms.move(SCENE)
        self.ctx.sources.use("stand", pos=(10.0, 0.0, 0.0), motion=0x30)
        tick(self.ctx)
        pose = wire.PoseState.unpack(self.link.of(Msg.POSE_STATE)[-1].payload)
        self.assertEqual((pose.pos, pose.motion0, pose.room), ((10.0, 0.0, 0.0), 0x30, SCENE))
        self.ctx.sources.use("held")
        self.ctx.sources.last_pose = pose
        tick(self.ctx, 1.0)
        self.assertTrue(wire.PoseState.unpack(self.link.of(Msg.POSE_STATE)[-1].payload).flags & wire.POSE_FLAG_HELD)
        before = len(self.link.sent)
        self.ctx.sources.use("silent")
        tick(self.ctx, 2.0)
        self.assertEqual(len(self.link.sent), before)

    def test_a_replay_plays_its_frames_when_due_and_translates_the_start(self):
        pose = wire.PoseState.pack(pos=(100.0, 0.0, 0.0), room=SCENE, quat=wire.IDENTITY_PACKED_QUAT)
        clip = {"frames": [dict(dt=0.0, type=Msg.POSE_STATE, payload=pose), dict(dt=10000.0, type=Msg.POSE_STATE, payload=pose)]}
        self.ctx.rooms.known.add(SCENE)
        self.ctx.rooms.move(SCENE)
        self.ctx.sources.use("replay", clip=clip, translate_to=(500.0, 0.0, 0.0))
        tick(self.ctx, 0.1)
        poses = self.link.of(Msg.POSE_STATE)
        self.assertEqual(len(poses), 1)
        self.assertEqual(wire.PoseState.unpack(poses[0].payload).pos, (500.0, 0.0, 0.0))

    def test_an_unknown_source_is_refused(self):
        with self.assertRaises(ValueError):
            self.ctx.sources.use("dream")


class GuardTests(unittest.TestCase):
    def test_a_state_type_is_refused_once_the_log_says_input_mode(self):
        ctx = make_context("guest")
        self.assertTrue(ctx.link.send(Msg.POSE_STATE, b"x"))
        ctx.store.add("adapter", "config: partner_mode=input")
        self.assertFalse(ctx.link.send(Msg.POSE_STATE, b"x"))
        self.assertTrue(ctx.link.send(Msg.PLAYER_STATE, b"x"))
        ctx.store.add("adapter", "config: partner_mode=state")
        self.assertTrue(ctx.link.send(Msg.POSE_STATE, b"x"))


class RoomTests(unittest.TestCase):
    def setUp(self):
        self.ctx = make_context("guest")
        self.link = self.ctx.link
        pose_from_game(self.ctx, SCENE)
        self.ctx.rooms.move(SCENE)
        self.link.sent.clear()

    def follow(self, serial=5, source=SCENE, scene=OTHER):
        deliver(self.ctx, Msg.DOOR_FOLLOW, wire.DoorFollow.pack(serial=serial, source_scene=source, scene=scene, entry=1,
                                                                character=wire.REBECCA))

    def results(self):
        return [(r.serial, wire.DOOR_RESULTS[r.result]) for r in map(lambda f: wire.DoorFollowResult.unpack(f.payload),
                                                                   self.link.of(Msg.DOOR_FOLLOW_RESULT))]

    def test_a_door_in_the_fakes_room_is_committed_then_followed_when_the_game_moves(self):
        self.follow()
        self.assertEqual(self.results(), [(5, "commit")])
        deliver(self.ctx, Msg.ROOM_MOVE, wire.RoomMove.pack(seq=1, scene=OTHER, entry=1, load_serial=1, character=wire.REBECCA))
        move = wire.RoomMove.unpack(self.link.of(Msg.ROOM_MOVE)[-1].payload)
        self.assertEqual((move.scene, move.mode, self.ctx.peer.place.scene), (OTHER, 2, OTHER))

    def test_a_timed_follow_moves_without_waiting_for_the_game(self):
        self.ctx.rooms.door_ms = 0.0
        self.follow()
        tick(self.ctx, 0.01)
        self.assertEqual(self.ctx.peer.place.scene, OTHER)

    def test_busy_other_room_and_no_answer(self):
        self.ctx.rooms.answer = "busy"
        self.follow(1)
        self.ctx.rooms.answer = "none"
        self.follow(2)
        self.ctx.rooms.answer = "commit"
        self.follow(3, source=OTHER)
        self.assertEqual(self.results(), [(1, "busy"), (3, "other room")])

    def test_the_echo_source_leaves_the_move_to_the_echo(self):
        self.ctx.sources.use("echo")
        self.follow()
        deliver(self.ctx, Msg.ROOM_MOVE, wire.RoomMove.pack(seq=1, scene=OTHER, entry=1, load_serial=1, character=wire.REBECCA))
        self.assertEqual(self.ctx.peer.place.scene, SCENE)

    def test_a_scene_the_game_never_named_is_refused(self):
        with self.assertRaises(UnknownScene):
            self.ctx.rooms.move(0x40)
        with self.assertRaises(UnknownScene):
            self.ctx.rooms.probe_self_heal(0x40)

    def test_moving_to_the_same_room_changes_nothing(self):
        serial = self.ctx.peer.place.load_serial
        self.assertFalse(self.ctx.rooms.move(SCENE))
        self.assertEqual(self.ctx.peer.place.load_serial, serial)

    def test_a_move_sends_room_move_and_room_state_with_a_new_load_serial(self):
        self.ctx.rooms.known.add(OTHER)
        serial = self.ctx.peer.place.load_serial
        self.ctx.rooms.move(OTHER, entry=3)
        move = wire.RoomMove.unpack(self.link.of(Msg.ROOM_MOVE)[0].payload)
        state = wire.RoomState.unpack(self.link.of(Msg.ROOM_STATE)[0].payload)
        self.assertEqual((move.entry, move.load_serial, state.scene), (3, serial + 1, OTHER))

    def test_the_fakes_own_door_comes_from_the_library_and_moves_it_on_a_rejection(self):
        library = Library()
        follow = wire.DoorFollow.make(serial=1, source_scene=SCENE, scene=OTHER, entry=2, type=4, motion=5, flag=0)
        library.doors[(SCENE, OTHER)] = {"run": "r", "follow": follow._asdict(), "arrival": (0.0, 0.0, 0.0)}
        self.ctx.library = library
        self.assertTrue(self.ctx.rooms.run_door(SCENE))
        sent = wire.DoorFollow.unpack(self.link.of(Msg.DOOR_FOLLOW)[0].payload)
        self.assertEqual((sent.scene, sent.entry, sent.type, sent.motion, sent.character), (OTHER, 2, 4, 5, wire.BILLY))
        deliver(self.ctx, Msg.DOOR_FOLLOW_RESULT, wire.DoorFollowResult.pack(serial=sent.serial, result=wire.RESULT_BUSY,
                                                                              character=wire.REBECCA))
        self.assertEqual(self.ctx.peer.place.scene, OTHER)

    def test_a_door_race_sends_the_fakes_own_door_and_answers_busy(self):
        self.ctx.rooms.race = True
        self.follow()
        self.assertEqual(len(self.link.of(Msg.DOOR_FOLLOW)), 1)
        self.assertEqual(self.results(), [(5, "busy")])

    def test_no_library_door_means_nothing_is_run(self):
        self.assertFalse(self.ctx.rooms.run_door(SCENE))


if __name__ == "__main__":
    unittest.main()
