"""Session flows, the room authority actions, the item ledger, the flag sequencer, props and the story messages."""
import time
import unittest

from fake_peer import wire
from fake_peer.library import Library
from fake_peer.session import judge_ready
from fake_peer.tests.support import deliver, make_context, pose_from_game, tick
from fake_peer.wire import Msg

SCENE, OTHER = 0x2D, 0x2E
HOST, GUEST = 0, 1


class JudgeReadyTests(unittest.TestCase):
    def ready(self, **fields):
        return wire.SessionReady.make(**dict(dict(scene=SCENE, load_serial=3, snapshot_serial=1), **fields))

    def pose(self, **fields):
        return wire.PoseState.make(**dict(dict(room=SCENE, load_serial=3), **fields))

    def test_the_session_ready_rule(self):
        self.assertIsNone(judge_ready(self.ready(), self.pose(), 100.0, 1))
        self.assertEqual(judge_ready(self.ready(snapshot_serial=2), self.pose(), 100.0, 1), "unknown snapshot")
        self.assertEqual(judge_ready(self.ready(), None, 0.0, 1), "no pose")
        self.assertEqual(judge_ready(self.ready(), self.pose(), 2500.0, 1), "stale pose")
        self.assertEqual(judge_ready(self.ready(scene=OTHER), self.pose(), 100.0, 1), "other room")
        self.assertEqual(judge_ready(self.ready(load_serial=4), self.pose(), 100.0, 1), "other load")


class HostSessionTests(unittest.TestCase):
    def setUp(self):
        self.ctx = make_context("host")
        self.link = self.ctx.link
        pose_from_game(self.ctx, SCENE, load_serial=3)
        self.ctx.rooms.move(SCENE)
        self.link.sent.clear()

    def ready(self, **fields):
        deliver(self.ctx, Msg.SESSION_READY, wire.SessionReady.pack(**dict(dict(
            phase=wire.READY, scene=SCENE, load_serial=3, snapshot_serial=1), **fields)))

    def test_a_snapshot_request_gets_snapshot_floor_and_extras_with_rising_serials(self):
        deliver(self.ctx, Msg.SNAPSHOT_REQUEST, b"\x2d\x00")
        deliver(self.ctx, Msg.SNAPSHOT_REQUEST, b"\x2d\x00")
        self.assertEqual([f.type for f in self.link.sent[:3]], [Msg.JOIN_SNAPSHOT, Msg.FLOOR_SNAPSHOT, Msg.JOIN_EXTRAS])
        self.assertEqual(len(self.link.sent[0].payload), wire.JOIN_SNAPSHOT_SIZE)
        serials = [wire.unpack_extras(f.payload)[0].snapshot_serial for f in self.link.of(Msg.JOIN_EXTRAS)]
        self.assertEqual(serials, [1, 2])

    def test_an_accepted_ready_is_echoed_with_the_same_values(self):
        deliver(self.ctx, Msg.SNAPSHOT_REQUEST, b"\x2d\x00")
        self.link.sent.clear()
        self.ready()
        echo = wire.SessionReady.unpack(self.link.of(Msg.SESSION_READY)[0].payload)
        self.assertEqual((echo.phase, echo.scene, echo.load_serial, echo.snapshot_serial), (wire.ECHO, SCENE, 3, 1))

    def test_every_refusal_is_recorded_and_never_echoed(self):
        deliver(self.ctx, Msg.SNAPSHOT_REQUEST, b"\x2d\x00")
        self.link.sent.clear()
        self.ready(snapshot_serial=9)
        self.ready(scene=OTHER)
        self.ready(load_serial=4)
        self.assertEqual([r[0] for r in self.ctx.flows.refusals], ["unknown snapshot", "other room", "other load"])
        self.assertEqual(self.link.of(Msg.SESSION_READY), [])

    def test_a_silent_pose_makes_the_ready_stale_and_no_echo_mode_stays_quiet(self):
        deliver(self.ctx, Msg.SNAPSHOT_REQUEST, b"\x2d\x00")
        self.link.sent.clear()
        self.ctx.remote.pose_arrival -= 3.0
        self.ready()
        self.assertEqual(self.ctx.flows.refusals[-1][0], "stale pose")
        self.ctx.remote.pose_arrival = time.monotonic()
        self.ctx.flows.no_echo = True
        self.ready()
        self.assertEqual(self.link.of(Msg.SESSION_READY), [])

    def test_the_host_status_goes_out_every_second(self):
        host = make_context("host", status=True)
        host.peer.place.scene = SCENE
        tick(host)
        types = {f.type for f in host.link.sent}
        self.assertTrue({Msg.PLAYER_STATE, Msg.ROOM_STATE, Msg.OWNERSHIP, Msg.PARTY_MODE, Msg.SAVE_SLOT} <= types)
        owners = wire.Ownership.unpack(host.link.of(Msg.OWNERSHIP)[0].payload)
        self.assertEqual((owners.rebecca_owner, owners.billy_owner), (HOST, GUEST))


class GuestSessionTests(unittest.TestCase):
    def setUp(self):
        self.ctx = make_context("guest")
        self.link = self.ctx.link
        pose_from_game(self.ctx, SCENE, load_serial=3)
        self.ctx.rooms.move(SCENE)
        self.link.sent.clear()

    def test_the_request_repeats_every_two_seconds_until_a_snapshot_arrives(self):
        self.ctx.flows.request_snapshot()
        tick(self.ctx)
        tick(self.ctx, 1.0)
        self.assertEqual(len(self.link.of(Msg.SNAPSHOT_REQUEST)), 1)
        tick(self.ctx, 2.5)
        self.assertEqual(len(self.link.of(Msg.SNAPSHOT_REQUEST)), 2)

    def test_extras_arm_the_ready_and_the_echo_stops_it(self):
        self.ctx.flows.request_snapshot()
        deliver(self.ctx, Msg.JOIN_EXTRAS, wire.pack_extras(4, 9, [dict(id=SCENE, epoch=2, owner=HOST)], [], []))
        tick(self.ctx)
        ready = wire.SessionReady.unpack(self.link.of(Msg.SESSION_READY)[0].payload)
        self.assertEqual((ready.phase, ready.scene, ready.snapshot_serial), (wire.READY, SCENE, 4))
        self.assertEqual((self.ctx.authority.table.owner_of(SCENE), self.ctx.flags.serial), (HOST, 9))
        deliver(self.ctx, Msg.SESSION_READY, wire.SessionReady.pack(phase=wire.ECHO, scene=SCENE, load_serial=ready.load_serial,
                                                                  snapshot_serial=4))
        self.link.sent.clear()
        tick(self.ctx, 1.5)
        self.assertEqual(self.link.of(Msg.SESSION_READY), [])

    def test_a_resync_request_from_the_host_asks_for_a_new_snapshot(self):
        deliver(self.ctx, Msg.RESYNC_REQUEST, b"")
        tick(self.ctx)
        self.assertEqual(len(self.link.of(Msg.SNAPSHOT_REQUEST)), 1)

    def test_the_snapshot_places_the_fake_where_the_host_saw_billy(self):
        self.ctx.rooms.known.add(OTHER)
        snapshot = bytearray(wire.JOIN_SNAPSHOT_SIZE)
        snapshot[2:4] = OTHER.to_bytes(2, "little")  # Billy's place scene
        deliver(self.ctx, Msg.JOIN_SNAPSHOT, bytes(snapshot))
        self.assertEqual(self.ctx.peer.place.scene, OTHER)

    def test_bad_readies_name_the_wrong_thing(self):
        self.ctx.rooms.known.add(OTHER)
        self.ctx.flows.send_bad_ready("other_room")
        self.ctx.flows.send_bad_ready("other_load")
        self.ctx.flows.send_bad_ready("unknown_snapshot")
        sent = [wire.SessionReady.unpack(f.payload) for f in self.link.of(Msg.SESSION_READY)]
        self.assertEqual(sent[0].scene, OTHER)
        self.assertNotEqual(sent[1].load_serial, self.ctx.peer.place.load_serial)
        self.assertGreaterEqual(sent[2].snapshot_serial, 1000)


class AuthorityActionTests(unittest.TestCase):
    def setUp(self):
        self.ctx = make_context("guest")
        self.auth = self.ctx.authority
        self.link = self.ctx.link
        pose_from_game(self.ctx, SCENE)
        self.auth.claim_delay_s = 0.0
        self.auth.mode = "normal"

    def test_reconcile_claims_the_room_it_stands_in_and_releases_one_it_left(self):
        self.ctx.rooms.move(SCENE)
        self.auth.reconcile()
        self.assertEqual(wire.AuthMessage.unpack(self.link.of(Msg.ROOM_CLAIM)[0].payload), wire.AuthMessage.make(id=SCENE, epoch=1, owner=GUEST))
        self.ctx.rooms.known.add(OTHER)
        self.ctx.rooms.move(OTHER)
        self.auth.reconcile()
        self.assertEqual(len(self.link.of(Msg.ROOM_STOP)), 1)
        self.assertEqual(self.auth.table.owner_of(OTHER), GUEST)

    def test_tie_mode_claims_on_arrival_before_the_game_is_heard(self):
        self.auth.mode = "tie"
        self.ctx.rooms.move(SCENE)
        self.assertEqual(len(self.link.of(Msg.ROOM_CLAIM)), 1)
        deliver(self.ctx, Msg.ROOM_CLAIM, wire.AuthMessage.pack(id=SCENE, epoch=1, owner=HOST), slot=HOST)
        self.assertEqual(self.auth.table.owner_of(SCENE), HOST)  # the lower slot wins

    def test_a_claim_over_the_games_room_needs_force_and_is_one_epoch_up(self):
        deliver(self.ctx, Msg.ROOM_CLAIM, wire.AuthMessage.pack(id=SCENE, epoch=1, owner=HOST), slot=HOST)
        self.auth.claim_scene(SCENE)
        self.assertEqual(self.link.of(Msg.ROOM_CLAIM), [])
        self.auth.force_claim(SCENE)
        self.assertEqual(wire.AuthMessage.unpack(self.link.of(Msg.ROOM_CLAIM)[0].payload).epoch, 2)

    def test_a_refused_message_is_recorded(self):
        deliver(self.ctx, Msg.ROOM_STOP, wire.AuthMessage.pack(id=SCENE, epoch=1, owner=HOST), slot=HOST)
        self.assertEqual(self.auth.refused[0][2], "unknown object")

    def test_a_new_peer_hears_the_fakes_claims_again(self):
        self.ctx.rooms.move(SCENE)
        self.auth.reconcile()
        self.link.sent.clear()
        self.auth._peer_was_up = False
        self.auth.tick(time.monotonic())
        self.assertEqual(len(self.link.of(Msg.ROOM_CLAIM)), 1)


class ItemTests(unittest.TestCase):
    KEY = (SCENE, 0x10, (10.0, 0.0, 0.0))

    def test_the_ledger_grants_the_first_asker_claims_the_second_and_remembers_a_take(self):
        from fake_peer.world import Ledger
        ledger = Ledger()
        self.assertEqual(ledger.request(self.KEY, GUEST, 0.0)[0], wire.ANSWER_GRANTED)
        self.assertEqual(ledger.request(self.KEY, GUEST, 1.0)[0], wire.ANSWER_GRANTED)
        self.assertEqual(ledger.request(self.KEY, HOST, 2.0)[0], wire.ANSWER_CLAIMED)
        self.assertEqual(ledger.request((SCENE, 0x10, (14.0, 0.0, 0.0)), HOST, 3.0)[0], wire.ANSWER_CLAIMED)  # within 10 units
        self.assertEqual(ledger.request(self.KEY, HOST, 20.0)[0], wire.ANSWER_GRANTED)  # the reservation ran out
        ledger.taken(self.KEY)
        self.assertEqual(ledger.request(self.KEY, GUEST, 21.0)[0], wire.ANSWER_TAKEN)
        ledger.put(self.KEY)
        self.assertEqual(ledger.consumed_keys(), [])

    def test_a_host_fake_answers_after_its_delay_and_forced_answers_win(self):
        ctx = make_context("host")
        ctx.items.answer_ms = 0.0
        request = wire.TakeRequest.pack(seq=7, scene=SCENE, item_id=0x10, count=1, pos=(10.0, 0.0, 0.0))
        deliver(ctx, Msg.TAKE_REQUEST, request)
        tick(ctx)
        result = wire.TakeResult.unpack(ctx.link.of(Msg.TAKE_RESULT)[0].payload)
        self.assertEqual((result.seq, result.requester, result.answer), (7, GUEST, wire.ANSWER_GRANTED))
        ctx.items.forced = "claimed"
        deliver(ctx, Msg.TAKE_REQUEST, request)
        tick(ctx)
        self.assertEqual(wire.TakeResult.unpack(ctx.link.of(Msg.TAKE_RESULT)[1].payload).answer, wire.ANSWER_CLAIMED)
        ctx.items.forced = "none"
        deliver(ctx, Msg.TAKE_REQUEST, request)
        tick(ctx)
        self.assertEqual(len(ctx.link.of(Msg.TAKE_RESULT)), 2)

    def test_a_guest_fake_sends_a_floor_take_for_a_granted_request(self):
        ctx = make_context("guest")
        ctx.peer.place.scene = SCENE
        ctx.items.learn(*self.KEY)
        self.assertEqual(ctx.items.take(), self.KEY)
        seq = wire.TakeRequest.unpack(ctx.link.of(Msg.TAKE_REQUEST)[0].payload).seq
        deliver(ctx, Msg.TAKE_RESULT, wire.TakeResult.pack(serial=1, seq=seq, scene=SCENE, requester=GUEST, answer=wire.ANSWER_GRANTED))
        self.assertEqual(len(ctx.link.of(Msg.FLOOR_TAKE)), 1)
        self.assertEqual(ctx.items.results, [(seq, wire.ANSWER_GRANTED)])
        deliver(ctx, Msg.TAKE_RESULT, wire.TakeResult.pack(serial=1, seq=seq + 5, scene=SCENE, requester=GUEST, answer=0))
        self.assertEqual(len(ctx.items.results), 1)  # not one of its requests

    def test_the_race_fires_on_the_pickup_motion_of_the_body(self):
        ctx = make_context("guest")
        ctx.peer.place.scene = SCENE
        ctx.items.learn(*self.KEY)
        ctx.items.race()
        deliver(ctx, Msg.MOTION_EVENT, wire.MotionEvent.pack(motion_no=0x14, layer=0x0100))  # a sub-control, not the body
        self.assertEqual(ctx.link.of(Msg.TAKE_REQUEST), [])
        deliver(ctx, Msg.MOTION_EVENT, wire.MotionEvent.pack(motion_no=0x14, layer=0))
        self.assertEqual(len(ctx.link.of(Msg.TAKE_REQUEST)), 1)

    def test_floor_items_the_game_named_feed_the_snapshot_and_the_consumed_list(self):
        ctx = make_context("host")
        deliver(ctx, Msg.FLOOR_PUT, wire.FloorPut.pack(room=SCENE, item_id=4, count=2, pos=(1.0, 2.0, 3.0)))
        self.assertEqual(ctx.items.floor_for_snapshot()[0]["item_id"], 4)
        deliver(ctx, Msg.FLOOR_TAKE, wire.FloorTake.pack(room=SCENE, item_id=4, pos=(1.0, 2.0, 3.0)))
        self.assertEqual(ctx.items.floor_for_snapshot(), [])
        self.assertEqual(ctx.items.consumed_dicts()[0]["item_id"], 4)


class FlagTests(unittest.TestCase):
    CHANGE = dict(word=3, set=0x10, clear=0)

    def commits(self, ctx):
        return [wire.unpack_flag_commit(f.payload) for f in ctx.link.of(Msg.FLAG_COMMIT)]

    def test_the_host_commits_every_proposal_with_the_guests_origin_and_serial_plus_one(self):
        ctx = make_context("host")
        deliver(ctx, Msg.FLAG_PROPOSE, wire.pack_flag_propose([self.CHANGE]))
        deliver(ctx, Msg.FLAG_PROPOSE, wire.pack_flag_propose([self.CHANGE]))
        (first, changes), (second, again) = self.commits(ctx)
        self.assertEqual((first.serial, first.origin, len(changes), changes[0].set), (1, GUEST, 1, 0x10))
        self.assertEqual((second.serial, len(again)), (2, 0))  # the second is a no-op: an empty commit

    def test_correct_inverts_the_next_proposal_and_gap_skips_a_serial(self):
        ctx = make_context("host")
        ctx.flags.words[3] = 0x10
        ctx.flags.correct_next = True
        deliver(ctx, Msg.FLAG_PROPOSE, wire.pack_flag_propose([dict(word=3, set=0, clear=0x10)]))
        changes = self.commits(ctx)[0][1]
        self.assertEqual((changes[0].set, changes[0].clear), (0x10, 0))  # the opposite of clearing a set bit, sent although the mirror holds it
        ctx.flags.gap_next = True
        deliver(ctx, Msg.FLAG_PROPOSE, wire.pack_flag_propose([self.CHANGE]))
        self.assertEqual(self.commits(ctx)[-1][0].serial, 3)

    def test_the_guest_default_proposal_is_a_no_op_and_commits_are_checked(self):
        ctx = make_context("guest")
        ctx.flags.propose()
        proposal = wire.unpack_flag_propose(ctx.link.of(Msg.FLAG_PROPOSE)[0].payload)[0]
        self.assertEqual((proposal.set, proposal.clear), (0, 1))  # bit 0 of word 0 is clear: clearing it changes nothing
        deliver(ctx, Msg.FLAG_COMMIT, wire.pack_flag_commit(1, GUEST, []))
        self.assertEqual(ctx.flags.proposed, [])
        deliver(ctx, Msg.FLAG_COMMIT, wire.pack_flag_commit(3, HOST, []))
        self.assertEqual(len(ctx.flags.violations), 1)

    def test_cross_proposes_the_bit_the_host_just_changed(self):
        ctx = make_context("guest")
        ctx.flags.cross = True
        deliver(ctx, Msg.FLAG_COMMIT, wire.pack_flag_commit(1, HOST, [wire.FlagChange.make(**self.CHANGE)._asdict()]))
        proposal = wire.unpack_flag_propose(ctx.link.of(Msg.FLAG_PROPOSE)[0].payload)[0]
        self.assertEqual((proposal.word, proposal.set), (3, 0x10))
        self.assertEqual(ctx.flags.words[3], 0x10)


class PropTests(unittest.TestCase):
    def request(self, ctx, slot, op=wire.LEASE_REQUEST):
        deliver(ctx, Msg.PROP_LEASE, wire.PropLease.pack(scene=SCENE, unit=2, op=op, holder=slot), slot=slot)

    def test_the_host_grants_the_first_asker_denies_the_second_and_commits_on_release(self):
        ctx = make_context("host")
        self.request(ctx, GUEST)
        grant = wire.PropLease.unpack(ctx.link.of(Msg.PROP_LEASE)[0].payload)
        self.assertEqual((grant.op, grant.holder, grant.serial), (wire.LEASE_GRANT, GUEST, 1))
        deliver(ctx, Msg.PROP_MOVE, wire.PropMove.pack(sender_ms=5, scene=SCENE, kind=wire.PROP_OBSTACLE_KIND, unit=2, pos=(1.0, 2.0, 3.0)), slot=GUEST)
        self.request(ctx, GUEST, wire.LEASE_RELEASE)
        commit = wire.PropCommit.unpack(ctx.link.of(Msg.PROP_COMMIT)[0].payload)
        self.assertEqual((commit.committer, commit.pos), (GUEST, (1.0, 2.0, 3.0)))

    def test_a_move_without_a_lease_is_a_violation_and_a_busy_box_is_denied(self):
        ctx = make_context("host")
        deliver(ctx, Msg.PROP_MOVE, wire.PropMove.pack(scene=SCENE, kind=wire.PROP_OBSTACLE_KIND, unit=2), slot=GUEST)
        self.assertEqual(len(ctx.props.violations), 1)
        self.request(ctx, GUEST)
        ctx.props.leases[(SCENE, 2)]["holder"] = HOST
        self.request(ctx, GUEST)
        deny = wire.PropLease.unpack(ctx.link.of(Msg.PROP_LEASE)[-1].payload)
        self.assertEqual((deny.op, deny.holder), (wire.LEASE_DENY, HOST))

    def test_a_guest_push_streams_ten_moves_ends_reliably_and_releases(self):
        library = Library()
        library.boxes[(SCENE, 2)] = {"run": "r", "pos": (0.0, 0.0, 0.0), "yaw": 0.0}
        ctx = make_context("guest", library)
        self.assertTrue(ctx.props.push(SCENE, 2))
        deliver(ctx, Msg.PROP_LEASE, wire.PropLease.pack(serial=1, scene=SCENE, unit=2, op=wire.LEASE_GRANT, holder=GUEST), slot=HOST)
        for step in range(12):
            tick(ctx, step * 0.11 + 0.01)
        moves = ctx.link.of(Msg.PROP_MOVE)
        self.assertEqual(len(moves), 10)
        self.assertTrue(moves[-1].flags & wire.FLAG_RELIABLE and not moves[0].flags & wire.FLAG_RELIABLE)
        self.assertEqual(wire.PropLease.unpack(ctx.link.of(Msg.PROP_LEASE)[-1].payload).op, wire.LEASE_RELEASE)
        self.assertIsNotNone(ctx.props.last_push_pos)

    def test_nothing_is_pushed_without_a_known_box_pose_and_a_deny_is_recorded(self):
        ctx = make_context("guest")
        self.assertFalse(ctx.props.push(SCENE, 2))
        deliver(ctx, Msg.PROP_LEASE, wire.PropLease.pack(scene=SCENE, unit=2, op=wire.LEASE_DENY, holder=HOST), slot=HOST)
        self.assertEqual(ctx.props.denied, [((SCENE, 2), HOST)])


class StoryTests(unittest.TestCase):
    def test_death_and_skip_messages(self):
        ctx = make_context("guest")
        ctx.story.die()
        self.assertEqual(ctx.link.of(Msg.PLAYER_DIED)[0].payload, bytes((wire.BILLY,)))
        ctx.story.request_skip(4, 7)
        skip = wire.DemoSkip.unpack(ctx.link.of(Msg.DEMO_SKIP_REQUEST)[0].payload)
        self.assertEqual((skip.demo_id, skip.serial, skip.flags), (4, 7, 1))
        deliver(ctx, Msg.DEMO_SKIP, wire.DemoSkip.pack(demo_id=4, serial=7))
        self.assertEqual(ctx.story.skips[0][0], "DEMO_SKIP")

    def test_a_door_state_reaches_room_state_and_the_poses_and_clears_again(self):
        ctx = make_context("guest")
        pose_from_game(ctx, SCENE)
        ctx.rooms.move(SCENE)
        ctx.rooms.known.add(OTHER)
        ctx.sources.use("stand", pos=(1.0, 0.0, 0.0))
        ctx.story.in_door(OTHER)
        state = wire.RoomState.unpack(ctx.link.of(Msg.ROOM_STATE)[-1].payload)
        self.assertEqual((state.door_target, state.door_flags), (OTHER, 1))
        self.assertEqual(ctx.sources.mode, "held")
        ctx.story.clear_door()
        self.assertEqual((ctx.sources.mode, wire.RoomState.unpack(ctx.link.of(Msg.ROOM_STATE)[-1].payload).door_target), ("stand", wire.NO_SCENE))

    def test_a_busy_think_state_is_reported_in_every_pose(self):
        ctx = make_context("guest")
        ctx.rooms.known.add(SCENE)
        ctx.rooms.move(SCENE)
        ctx.sources.use("stand", pos=(1.0, 0.0, 0.0))
        ctx.story.busy_think()
        tick(ctx)
        self.assertEqual(wire.PoseState.unpack(ctx.link.of(Msg.POSE_STATE)[-1].payload).group, 2)

    def test_an_unnamed_door_target_is_refused(self):
        ctx = make_context("guest")
        with self.assertRaises(ValueError):
            ctx.story.in_door(0x40)


if __name__ == "__main__":
    unittest.main()
