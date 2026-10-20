import struct
import unittest

from fake_peer import golden, wire, wire_check
from fake_peer.wire import Msg


class LayoutTests(unittest.TestCase):
    def test_every_layout_round_trips_and_has_its_header_size(self):
        for layout in wire.LAYOUTS:
            record = layout.make()
            self.assertEqual(layout.unpack(layout.pack(**record._asdict())), record, layout.name)
            self.assertEqual(len(layout.pack()), layout.size)

    def test_a_payload_of_the_wrong_size_is_refused(self):
        with self.assertRaises(wire.WireError):
            wire.PoseState.unpack(bytes(wire.PoseState.size - 1))
        with self.assertRaises(wire.WireError):
            wire.PoseState.pack(no_such_field=1)

    def test_grouped_fields_come_back_as_tuples(self):
        pose = wire.PoseState.make(pos=(1.0, 2.0, 3.0))
        self.assertEqual(pose.pos, (1.0, 2.0, 3.0))

    def test_headers_and_ids_match_the_adapter(self):
        self.assertEqual(wire_check.check_all(), [])


class QuaternionTests(unittest.TestCase):
    def test_identity_and_a_turn_survive_packing(self):
        for quat in (wire.IDENTITY_QUAT, (0.0, 0.7071068, 0.0, 0.7071068), (0.5, -0.5, 0.5, 0.5)):
            back = wire.unpack_quat(wire.pack_quat(quat))
            sign = 1.0 if sum(a * b for a, b in zip(quat, back)) >= 0 else -1.0
            for want, got in zip(quat, back):
                self.assertAlmostEqual(want, sign * got, places=2)

    def test_the_dropped_component_is_rebuilt_positive(self):
        back = wire.unpack_quat(wire.pack_quat((0.0, 0.0, 0.0, -1.0)))
        self.assertAlmostEqual(back[3], 1.0, places=3)


class FrameTests(unittest.TestCase):
    def test_a_frame_encodes_and_decodes(self):
        data = wire.encode_frame(Msg.POSE_STATE, b"abc", flags=0, slot=1)
        length, = struct.unpack_from("<I", data)
        self.assertEqual(length, 4 + 3)
        frame = wire.decode_body(data[4:])
        self.assertEqual((frame.type, frame.flags, frame.slot, frame.payload), (Msg.POSE_STATE, 0, 1, b"abc"))

    def test_the_session_control_payloads(self):
        welcome = wire.parse_welcome(wire.welcome_payload(1, 0, 2, 7))
        self.assertEqual(welcome, {"local_slot": 1, "host_slot": 0, "max_players": 2, "epoch": 7})
        self.assertEqual(wire.peer_up_payload(1, 5, "x")[0], 1)
        self.assertEqual(wire.hello_payload("re0")[:2], struct.pack("<H", wire.PROTO_VERSION))


class VariablePayloadTests(unittest.TestCase):
    def test_extras_round_trip(self):
        claim = dict(id=0x2D, epoch=3, owner=1)
        commit = dict(serial=2, scene=0x2D, kind=wire.PROP_OBSTACLE_KIND, unit=1, pos=(1.0, 2.0, 3.0), yaw=0.5, committer=1)
        item = dict(scene=0x2D, item_id=9, pos=(4.0, 5.0, 6.0))
        header, claims, props, items = wire.unpack_extras(wire.pack_extras(5, 7, [claim], [commit], [item]))
        self.assertEqual((header.snapshot_serial, header.flag_serial), (5, 7))
        self.assertEqual((claims[0].id, props[0].unit, items[0].item_id), (0x2D, 1, 9))

    def test_a_truncated_extras_payload_is_refused(self):
        with self.assertRaises(wire.WireError):
            wire.unpack_extras(wire.pack_extras(1, 0, [dict(id=1, epoch=1, owner=0)], [], [])[:-1])

    def test_enemy_batch_round_trip_and_size_check(self):
        entry = dict(slot=3, class_index=2, flags=wire.ENEMY_ALIVE, hp=50, record=(2, 1, 0, 0))
        payload = wire.pack_enemy_batch(dict(sender_ms=9, seq=2, room_epoch=1, scene=0x10), wire.EnemyPoseEntry, [entry, entry])
        header, entries = wire.unpack_enemy_batch(wire.EnemyPoseEntry, payload)
        self.assertEqual((header.count, header.seq, len(entries), entries[0].hp), (2, 2, 2, 50))
        with self.assertRaises(wire.WireError):
            wire.unpack_enemy_batch(wire.EnemyPoseEntry, payload[:-1])

    def test_flag_messages(self):
        change = dict(word=3, set=0x10, clear=0x01)
        self.assertEqual(wire.unpack_flag_propose(wire.pack_flag_propose([change]))[0].set, 0x10)
        header, changes = wire.unpack_flag_commit(wire.pack_flag_commit(4, 1, [change]))
        self.assertEqual((header.serial, header.origin, changes[0].clear), (4, 1, 1))

    def test_boss_and_floor_lists(self):
        header, members = wire.unpack_boss(wire.pack_boss(dict(token=1, scene=2), [dict(slot=1), dict(slot=2)]))
        self.assertEqual((header.count, [m.slot for m in members]), (2, [1, 2]))
        _, entries = wire.unpack_floor_snapshot(wire.pack_floor_snapshot([dict(room=2, item_id=3)]))
        self.assertEqual(entries[0].item_id, 3)

    def test_weapon_fx_positions_shift_by_entry_type(self):
        header = wire.WeaponFxHeader.pack(sender_ms=1, seq=1, character=0, count=3)
        se = struct.pack("<BBHf", 1, 0, 0x1E00, 1.0)
        ripple = struct.pack("<BBBB3f", 5, 30, 0, 0, 10.0, 0.0, 0.0)
        casing = struct.pack("<BBBB", 3, 19, 1, 1)
        shifted = wire.shift_weapon_fx_x(header + se + ripple + casing, 150.0)
        self.assertEqual(struct.unpack_from("<f", shifted, wire.WeaponFxHeader.size + len(se) + 4)[0], 160.0)
        self.assertEqual(shifted[-4:], casing)


class GoldenTests(unittest.TestCase):
    def test_the_file_is_what_the_generator_writes_now(self):
        self.assertTrue(golden.is_current())

    def test_every_state_type_has_an_accepted_and_a_refused_sample(self):
        seen = {}
        for msg_type, expect, _ in golden.samples():
            seen.setdefault(msg_type, set()).add(expect)
        for message in Msg:
            if wire.is_state_type(message.value) and message in wire.CPP_IDS and message not in (Msg.ROOM_STATE,):
                self.assertEqual(seen.get(message.value), {0, 1}, message.name)


if __name__ == "__main__":
    unittest.main()
