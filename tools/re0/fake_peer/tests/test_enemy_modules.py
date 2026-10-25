"""Enemy observer / shooter / replay, contact and lease, and the boss hand-over, with the real modules on a recording link."""
import unittest

from fake_peer import wire, wire_check
from fake_peer.contact import ALLOWED_HITS, HIT_HP_STEP, accept_holder_pose, token_low
from fake_peer.peer import DEFAULT_HP
from fake_peer.library import Library
from fake_peer.tests.support import deliver, make_context, pose_from_game, tick
from fake_peer.wire import Msg

SCENE, OTHER = 0x2D, 0x2E
HOST, GUEST = 0, 1
VTABLES = wire_check.vtables("kEnemyVtables")
ZOMBIE, BOSS = 0, 14


def entry(slot=4, **fields):
    base = dict(slot=slot, class_index=ZOMBIE, flags=wire.ENEMY_ALIVE, target=wire.REBECCA, spawn_id=7, spawn_record=2, hp=90,
                pos=(300.0, 0.0, 0.0), quat=wire.IDENTITY_PACKED_QUAT, record=(2, 5, 0, 0))
    return dict(base, **fields)


def batch(entries, scene=SCENE, epoch=2, seq=1):
    return wire.pack_enemy_batch(dict(sender_ms=1000, seq=seq, room_epoch=epoch, scene=scene), wire.EnemyPoseEntry, entries)


def observer_context():
    """A guest fake standing in SCENE whose game (slot 0) holds the room at epoch 2."""
    ctx = make_context("guest")
    pose_from_game(ctx, SCENE)
    ctx.rooms.move(SCENE)
    deliver(ctx, Msg.ROOM_CLAIM, wire.AuthMessage.pack(id=SCENE, epoch=2, owner=HOST), slot=HOST)
    ctx.link.sent.clear()
    return ctx


def pose_batch(ctx, entries, **fields):
    deliver(ctx, Msg.ENEMY_POSE, batch(entries, **{k: v for k, v in fields.items() if k in ("scene", "epoch", "seq")}),
            slot=fields.get("slot", HOST), flags=fields.get("flags", 0))


class ObserverTests(unittest.TestCase):
    def test_a_valid_stream_binds_slots_and_raises_no_violation(self):
        ctx = observer_context()
        pose_batch(ctx, [entry(4), entry(5, spawn_id=8)], seq=1)
        pose_batch(ctx, [entry(4), entry(5, spawn_id=8)], seq=2)
        self.assertEqual((ctx.enemies.bound_slots(), ctx.enemies.violations), ([4, 5], []))

    def test_each_header_check_has_its_violation(self):
        for label, fields in (("not the holder", dict(slot=GUEST)), ("not loaded", dict(scene=OTHER)),
                              ("epoch", dict(epoch=1))):
            ctx = observer_context()
            ctx.rooms.known.add(OTHER)
            pose_batch(ctx, [entry()], **fields)
            self.assertEqual(len(ctx.enemies.violations), 3 if label == "not loaded" else 1, label)

    def test_a_repeated_seq_is_a_violation_but_a_reliable_batch_may_pass_a_held_one(self):
        ctx = observer_context()
        pose_batch(ctx, [entry()], seq=5)
        pose_batch(ctx, [entry(hp=80)], seq=6, flags=wire.FLAG_RELIABLE)
        pose_batch(ctx, [entry(hp=70)], seq=5)
        self.assertEqual(len(ctx.enemies.violations), 1)
        self.assertIn("seq 5 after 5", ctx.enemies.violations[0])

    def test_enemy_rules_hp_never_rises_terminal_is_reliable_a_lease_needs_a_lease(self):
        ctx = observer_context()
        pose_batch(ctx, [entry(hp=50)], seq=1)
        pose_batch(ctx, [entry(hp=60)], seq=2)
        pose_batch(ctx, [entry(flags=wire.ENEMY_TERMINAL, hp=-1)], seq=3, flags=0)
        pose_batch(ctx, [entry(flags=wire.ENEMY_ALIVE | wire.ENEMY_LEASED, lease_token=9)], seq=4)
        text = " ".join(ctx.enemies.violations)
        self.assertIn("hp rose", text)
        self.assertIn("terminal sent unreliably", text)
        self.assertIn("leased pose with no lease", text)

    def test_another_spawn_in_a_slot_starts_a_new_enemy_and_may_have_more_hp(self):
        ctx = observer_context()
        pose_batch(ctx, [entry(hp=10)], seq=1)
        pose_batch(ctx, [entry(spawn_id=99, hp=200)], seq=2)
        self.assertEqual(ctx.enemies.violations, [])
        self.assertEqual(ctx.enemies.observed[4].spawn_id, 99)

    def test_a_terminal_and_a_removal_are_counted(self):
        ctx = observer_context()
        pose_batch(ctx, [entry()], seq=1)
        pose_batch(ctx, [entry(flags=wire.ENEMY_TERMINAL, hp=-1)], seq=2, flags=wire.FLAG_RELIABLE)
        pose_batch(ctx, [entry(flags=wire.ENEMY_REMOVED, hp=-1)], seq=3, flags=wire.FLAG_RELIABLE)
        slot = ctx.enemies.observed[4]
        self.assertEqual((slot.terminals, slot.removed), (1, 1))

    def test_the_rate_is_judged_around_twenty_hertz(self):
        ctx = observer_context()
        ctx.enemies.pose_times = [i * 0.05 for i in range(40)]
        self.assertTrue(ctx.enemies.rate_ok())
        ctx.enemies.pose_times = [i * 0.1 for i in range(40)]
        self.assertFalse(ctx.enemies.rate_ok())

    def test_a_malformed_batch_is_counted_not_fatal(self):
        ctx = observer_context()
        deliver(ctx, Msg.ENEMY_POSE, batch([entry()])[:-3], slot=HOST, flags=0)
        self.assertEqual(ctx.dispatcher.malformed[Msg.ENEMY_POSE], 1)


class ShooterTests(unittest.TestCase):
    def library(self):
        library = Library()
        hit = wire.HitRequest.make(slot=1, attacker=wire.BILLY, room=9, attack_type=2, vtable=0, forwarded=1)
        library.hit_requests[(ZOMBIE, 2)] = {"run": "r", "payload": wire.HitRequest.pack(**hit._asdict())}
        return library

    def test_a_hit_request_is_rebuilt_for_the_target_slot_room_and_class(self):
        ctx = observer_context()
        ctx.library = self.library()
        pose_batch(ctx, [entry(4)], seq=1)
        self.assertTrue(ctx.enemies.shoot(4))
        hit = wire.HitRequest.unpack(ctx.link.of(Msg.HIT_REQUEST)[0].payload)
        self.assertEqual((hit.slot, hit.room, hit.vtable, hit.forwarded, hit.attack_type), (4, SCENE & 0xFF, VTABLES[ZOMBIE], 0, 2))

    def test_nothing_is_sent_for_an_unobserved_slot_or_without_a_capture(self):
        ctx = observer_context()
        self.assertFalse(ctx.enemies.shoot(4))
        pose_batch(ctx, [entry(4)], seq=1)
        self.assertFalse(ctx.enemies.shoot(4))
        self.assertEqual(ctx.link.of(Msg.HIT_REQUEST), [])

    def test_hp_check_compares_with_the_value_before_the_shot(self):
        ctx = observer_context()
        pose_batch(ctx, [entry(4, hp=90)], seq=1)
        ctx.enemies.arm_hp_check(4)
        self.assertFalse(ctx.enemies.shot_hp_lower())
        pose_batch(ctx, [entry(4, hp=60)], seq=2)
        self.assertTrue(ctx.enemies.shot_hp_lower())


class ReplayTests(unittest.TestCase):
    def setUp(self):
        self.ctx = make_context("guest")
        pose_from_game(self.ctx, SCENE)
        self.ctx.rooms.move(SCENE)
        self.ctx.authority.claim_scene(SCENE)
        self.clip = {"frames": [dict(dt=0.0, type=Msg.ENEMY_POSE, payload=batch([entry(4, flags=wire.ENEMY_ALIVE | wire.ENEMY_LEASED, lease_token=5),
                                                                                 entry(5, spawn_id=8)], epoch=1))]}
        self.ctx.link.sent.clear()

    def sent_entries(self):
        return [e for f in self.ctx.link.of(Msg.ENEMY_POSE) for e in wire.unpack_enemy_batch(wire.EnemyPoseEntry, f.payload)[1]]

    def test_the_clip_is_replayed_with_the_fakes_epoch_sequence_and_without_leases(self):
        self.assertTrue(self.ctx.enemies.start_replay(SCENE, self.clip))
        tick(self.ctx, 0.1)
        header = wire.unpack_enemy_batch(wire.EnemyPoseEntry, self.ctx.link.of(Msg.ENEMY_POSE)[0].payload)[0]
        self.assertEqual((header.room_epoch, header.scene, header.seq), (1, SCENE, 1))
        self.assertEqual([(e.flags, e.lease_token) for e in self.sent_entries()], [(wire.ENEMY_ALIVE, 0)] * 2)

    def test_three_hits_kill_with_one_reliable_terminal_then_a_throttled_corpse(self):
        self.ctx.enemies.start_replay(SCENE, self.clip, hits_to_kill=3)
        tick(self.ctx, 0.1)
        for number in range(3):
            deliver(self.ctx, Msg.HIT_REQUEST, wire.HitRequest.pack(slot=4, attacker=wire.REBECCA), flags=wire.FLAG_RELIABLE)
        self.assertEqual(len(self.ctx.enemies.hit_requests), 3)
        self.ctx.link.sent.clear()
        self.ctx.enemies._index = 0
        tick(self.ctx, 0.2)
        terminal = [f for f in self.ctx.link.of(Msg.ENEMY_POSE) if f.flags & wire.FLAG_RELIABLE]
        self.assertEqual(len(terminal), 1)
        shown = wire.unpack_enemy_batch(wire.EnemyPoseEntry, terminal[0].payload)[1][0]
        self.assertEqual((shown.slot, shown.flags, shown.hp), (4, wire.ENEMY_TERMINAL, -1))
        self.ctx.link.sent.clear()
        self.ctx.enemies._index = 0
        tick(self.ctx, 0.3)
        self.assertEqual([e.slot for e in self.sent_entries()], [5])  # no corpse again within a second

    def test_a_paused_replay_sends_nothing_and_the_probes_name_their_fault(self):
        self.ctx.enemies.start_replay(SCENE, self.clip)
        self.ctx.enemies.pause_replay()
        tick(self.ctx, 0.1)
        self.assertEqual(self.ctx.link.of(Msg.ENEMY_POSE), [])
        self.assertTrue(self.ctx.enemies.probe("binding"))
        self.assertTrue(self.ctx.enemies.probe("epoch"))
        binding, epoch = [wire.unpack_enemy_batch(wire.EnemyPoseEntry, f.payload) for f in self.ctx.link.of(Msg.ENEMY_POSE)]
        self.assertEqual(binding[1][0].spawn_id, 8)
        self.assertNotEqual(epoch[0].room_epoch, self.ctx.authority.table.epoch_of(SCENE))
        with self.assertRaises(ValueError):
            self.ctx.enemies.probe("nothing")

    def test_no_clip_means_no_replay(self):
        self.assertFalse(self.ctx.enemies.start_replay(OTHER))

    def test_the_nearest_living_enemy(self):
        self.ctx.enemies.start_replay(SCENE, self.clip)
        tick(self.ctx, 0.1)
        self.assertEqual(self.ctx.enemies.nearest_replay_slot((0.0, 0.0, 0.0)), 4)


class ContactVictimTests(unittest.TestCase):
    def setUp(self):
        self.ctx = observer_context()
        self.contact = self.ctx.contact
        self.grant = wire.EnemyLease.make(token=(1 << 16) | 3, room_epoch=2, scene=SCENE, slot=4, class_index=ZOMBIE, spawn_id=7,
                                          victim=wire.BILLY, hp=90, record=(2, 5, 0, 0), pos=(300.0, 0.0, 0.0),
                                          quat=wire.IDENTITY_PACKED_QUAT)

    def hit(self, **fields):
        base = dict(room_epoch=2, sender_ms=5, seq=1, scene=SCENE, slot=4, class_index=ZOMBIE, spawn_id=7, victim=wire.BILLY, kind=0, level=2)
        deliver(self.ctx, Msg.PLAYER_HIT, wire.PlayerHit.pack(**dict(base, **fields)))

    def test_a_hit_lowers_the_hp_once_and_each_check_refuses(self):
        self.hit()
        self.assertEqual((len(self.contact.hits), self.ctx.peer.hp), (1, DEFAULT_HP - HIT_HP_STEP))
        self.hit(seq=1)
        self.hit(seq=2, room_epoch=9)
        self.hit(seq=3, victim=wire.REBECCA)
        self.assertEqual([r for _, r in self.contact.refused_hits], ["seq", "epoch", "victim"])

    def grant_and_stream(self, mode, seconds):
        self.contact.lease_mode = mode
        self.contact.hold_s = 0.5
        deliver(self.ctx, Msg.ENEMY_LEASE, wire.EnemyLease.pack(**self.grant._asdict()))
        for step in range(int(seconds * 10)):
            tick(self.ctx, 0.1 * (step + 1))

    def leased_entries(self):
        return [e for f in self.ctx.link.of(Msg.ENEMY_POSE) for e in wire.unpack_enemy_batch(wire.EnemyPoseEntry, f.payload)[1]]

    def ends(self):
        return [wire.EnemyLeaseEnd.unpack(f.payload) for f in self.ctx.link.of(Msg.ENEMY_LEASE_END)]

    def test_hold_streams_the_grant_pose_with_the_token_then_releases_with_the_think_record(self):
        self.grant_and_stream("hold", 1.0)
        entries = self.leased_entries()
        self.assertTrue(entries and all(e.lease_token == 3 and e.flags & wire.ENEMY_LEASED and e.hp == 90 for e in entries))
        end = self.ends()[0]
        self.assertEqual((end.reason, end.record, end.hp), (wire.END_RELEASED, (2, 0, 0, 0), 90))

    def test_decline_ends_at_once_and_expire_says_nothing(self):
        self.grant_and_stream("decline", 0.2)
        self.assertEqual([e.reason for e in self.ends()], [wire.END_DECLINED])
        self.ctx.link.sent.clear()
        self.grant_and_stream("expire", 1.0)
        self.assertEqual((self.ends(), self.leased_entries()), ([], []))

    def test_silent_sends_one_pose_and_the_refused_modes_send_their_fault(self):
        self.grant_and_stream("silent", 1.0)
        self.assertEqual(len(self.leased_entries()), 1)
        for mode, check in (("bad-token", lambda e: e.lease_token != 3), ("bad-hp", lambda e: e.hp > 90),
                            ("jump", lambda e: e.pos[0] > 1000.0)):
            self.ctx.link.sent.clear()
            self.contact.held.clear()
            self.grant_and_stream(mode, 0.5)
            self.assertTrue(self.leased_entries() and all(check(e) for e in self.leased_entries()), mode)

    def test_cut_mode_cuts_the_link_and_a_hit_request_kills_the_held_enemy(self):
        self.grant_and_stream("cut", 0.1)
        self.assertFalse(self.ctx.shaper.idle())
        self.contact.held.clear()
        self.contact.lease_mode = "hold"
        deliver(self.ctx, Msg.ENEMY_LEASE, wire.EnemyLease.pack(**self.grant._asdict()))
        for _ in range(3):
            deliver(self.ctx, Msg.HIT_REQUEST, wire.HitRequest.pack(slot=4, attacker=wire.REBECCA))
        self.assertEqual(self.ends()[-1].reason, wire.END_ENEMY_DEAD)
        self.assertEqual(self.contact.held, {})


class ContactAuthorityTests(unittest.TestCase):
    def setUp(self):
        self.library = Library()
        grant = wire.EnemyLease.make(token=(1 << 16) | 1, hp=90, record=(6, 3, 0, 0), class_index=ZOMBIE)
        self.library.lease_grants[ZOMBIE] = {"run": "r", "payload": wire.EnemyLease.pack(**grant._asdict())}
        self.ctx = make_context("host", self.library)
        pose_from_game(self.ctx, SCENE, pos=(10.0, 0.0, 0.0))
        self.ctx.rooms.move(SCENE)
        self.ctx.authority.claim_scene(SCENE)
        clip = {"frames": [dict(dt=0.0, type=Msg.ENEMY_POSE, payload=batch([entry(4)], epoch=1))]}
        self.ctx.enemies.start_replay(SCENE, clip)
        tick(self.ctx, 0.1)
        self.ctx.link.sent.clear()
        self.contact = self.ctx.contact

    def test_a_plain_hit_comes_from_the_allow_list_against_the_games_character(self):
        self.assertTrue(self.contact.hit(4))
        hit = wire.PlayerHit.unpack(self.ctx.link.of(Msg.PLAYER_HIT)[0].payload)
        self.assertEqual((hit.victim, hit.kind, hit.level, hit.slot, hit.spawn_id), (wire.BILLY, 0, 2, 4, 7))
        self.assertEqual(hit.point, (0.0, 0.0, 100.0))
        with self.assertRaises(ValueError):
            self.contact.hit(4, kind=0, level=4)
        self.assertEqual(ALLOWED_HITS, ((0, 2), (0, 3)))

    def test_a_grab_is_the_recorded_grant_moved_in_front_of_the_victim(self):
        self.assertTrue(self.contact.grab(4))
        grant = wire.EnemyLease.unpack(self.ctx.link.of(Msg.ENEMY_LEASE)[0].payload)
        self.assertEqual((grant.record, grant.hp, grant.victim, grant.slot, grant.spawn_id), ((6, 3, 0, 0), 90, wire.BILLY, 4, 7))
        self.assertEqual(grant.pos, (70.0, 0.0, 0.0))
        self.assertEqual(token_low(grant.token), grant.token & 0xFFFF)
        self.assertNotEqual(token_low(grant.token), 0)

    def test_the_games_leased_poses_pass_the_authority_rule_or_are_refused(self):
        self.contact.grab(4)
        token = token_low(wire.EnemyLease.unpack(self.ctx.link.of(Msg.ENEMY_LEASE)[0].payload).token)

        def holder(**fields):
            pose_batch(self.ctx, [entry(4, flags=wire.ENEMY_ALIVE | wire.ENEMY_LEASED, lease_token=token, **fields)],
                       scene=SCENE, epoch=self.ctx.authority.table.epoch_of(SCENE), seq=self.ctx.peer.next("t"))

        holder(pos=(70.0, 0.0, 0.0), hp=90)
        self.assertEqual((self.contact.accepted_poses, self.contact.refused_poses), (1, []))
        holder(pos=(70.0, 0.0, 0.0), hp=95)
        holder(pos=(99999.0, 0.0, 0.0), hp=90)
        self.assertEqual([r for _, r in self.contact.refused_poses], ["hp or displacement"] * 2)
        pose_batch(self.ctx, [entry(4, flags=wire.ENEMY_ALIVE | wire.ENEMY_LEASED, lease_token=token ^ 1)], seq=50,
                   epoch=self.ctx.authority.table.epoch_of(SCENE))
        self.assertEqual(self.contact.refused_poses[-1][1], "wrong token")

    def test_the_game_ending_the_lease_removes_it_and_a_silent_holder_lapses(self):
        self.contact.grab(4)
        away = self.contact.away[4]
        deliver(self.ctx, Msg.ENEMY_LEASE_END, wire.EnemyLeaseEnd.pack(token=away.token, room_epoch=1, scene=SCENE, slot=4,
                                                                      reason=wire.END_RELEASED, hp=80))
        self.assertEqual(self.contact.away, {})
        self.contact.grab(4)
        self.ctx.link.sent.clear()
        tick(self.ctx, 2.0)
        self.assertEqual(wire.EnemyLeaseEnd.unpack(self.ctx.link.of(Msg.ENEMY_LEASE_END)[0].payload).reason, wire.END_EXPIRED)
        self.assertEqual(self.contact.away, {})

    def test_a_leased_pose_that_trails_the_lease_end_is_not_a_fault(self):
        self.contact.grab(4)
        away = self.contact.away[4]
        deliver(self.ctx, Msg.ENEMY_LEASE_END, wire.EnemyLeaseEnd.pack(token=away.token, room_epoch=1, scene=SCENE, slot=4,
                                                                      reason=wire.END_RELEASED, hp=80))
        pose_batch(self.ctx, [entry(4, flags=wire.ENEMY_ALIVE | wire.ENEMY_LEASED, lease_token=token_low(away.token))], seq=60,
                   epoch=self.ctx.authority.table.epoch_of(SCENE))
        self.assertEqual(self.contact.refused_poses, [])
        pose_batch(self.ctx, [entry(4, flags=wire.ENEMY_ALIVE | wire.ENEMY_LEASED, lease_token=token_low(away.token) ^ 1)], seq=61,
                   epoch=self.ctx.authority.table.epoch_of(SCENE))
        self.assertEqual(self.contact.refused_poses, [(4, "no lease")])

    def test_expire_holder_sends_the_end_once(self):
        self.contact.grab(4)
        self.assertTrue(self.contact.expire_holder(4))
        self.assertFalse(self.contact.expire_holder(4))

    def test_the_acceptance_rule(self):
        self.assertTrue(accept_holder_pose((0, 0, 0), (900, 0, 0), 0.0, 50, 90))
        self.assertFalse(accept_holder_pose((0, 0, 0), (1100, 0, 0), 0.0, 50, 90))
        self.assertTrue(accept_holder_pose((0, 0, 0), (13000, 0, 0), 1.0, 50, 90))
        self.assertFalse(accept_holder_pose((0, 0, 0), (0, 0, 0), 1.0, 91, 90))
        self.assertTrue(self.contact.lease_exists(0) is False)


class BossTests(unittest.TestCase):
    def prepare(self, **header):
        member = dict(slot=6, class_index=BOSS, spawn_id=9, spawn_record=2, hp=500, pos=(400.0, 0.0, 0.0), quat=wire.IDENTITY_PACKED_QUAT, record=(2, 5, 0, 0))
        fields = dict(token=(1 << 24) | 1, room_epoch=2, scene=SCENE, phase=wire.BOSS_PREPARE, alive=1)
        return wire.pack_boss(dict(fields, **header), [member])

    def new_authority(self):
        ctx = observer_context()
        pose_batch(ctx, [entry(6, class_index=BOSS, spawn_id=9, hp=500)], seq=1)
        return ctx

    def sent_phases(self, ctx):
        return [wire.unpack_boss(f.payload)[0] for f in ctx.link.of(Msg.BOSS_HANDOVER)]

    def test_a_matching_prepare_is_answered_ready_a_different_one_cancelled_as_member(self):
        ctx = self.new_authority()
        deliver(ctx, Msg.BOSS_HANDOVER, self.prepare())
        self.assertEqual([h.phase for h in self.sent_phases(ctx)], [wire.BOSS_READY])
        other = observer_context()
        deliver(other, Msg.BOSS_HANDOVER, self.prepare())
        cancel = self.sent_phases(other)[0]
        self.assertEqual((cancel.phase, cancel.reason), (wire.BOSS_CANCEL, 4))

    def test_none_stays_silent_and_leave_mid_walks_out_after_ready(self):
        ctx = self.new_authority()
        ctx.boss.answer = "none"
        deliver(ctx, Msg.BOSS_HANDOVER, self.prepare())
        self.assertEqual(ctx.link.of(Msg.BOSS_HANDOVER), [])
        ctx.boss.answer = "ready"
        ctx.rooms.known.add(OTHER)
        ctx.boss.leave_mid_to = OTHER
        deliver(ctx, Msg.BOSS_HANDOVER, self.prepare())
        self.assertEqual(ctx.peer.place.scene, OTHER)

    def test_after_the_commit_the_fake_waits_for_the_game_to_leave_then_holds_and_reports_a_kill(self):
        ctx = self.new_authority()
        deliver(ctx, Msg.BOSS_HANDOVER, self.prepare())
        deliver(ctx, Msg.BOSS_HANDOVER, wire.pack_boss(dict(token=(1 << 24) | 1, room_epoch=2, scene=SCENE, phase=wire.BOSS_COMMIT), []))
        tick(ctx, 0.1)
        self.assertEqual(ctx.boss.new["stage"], "committed")  # the game is still in the room
        pose_from_game(ctx, OTHER)
        deliver(ctx, Msg.ROOM_STOP, wire.AuthMessage.pack(id=SCENE, epoch=2, owner=HOST), slot=HOST)
        tick(ctx, 0.2)
        self.assertEqual(ctx.boss.new["stage"], "running")
        tick(ctx, 0.5)
        self.assertTrue(ctx.link.of(Msg.ENEMY_POSE))
        self.assertTrue(ctx.boss.kill(0))
        outcome = self.sent_phases(ctx)[-1]
        self.assertEqual((outcome.phase, outcome.alive), (wire.BOSS_OUTCOME, 0))

    def test_the_old_authority_offers_its_boss_members_and_commits_on_ready(self):
        ctx = make_context("host")
        pose_from_game(ctx, SCENE)
        ctx.rooms.move(SCENE)
        ctx.authority.claim_scene(SCENE)
        ctx.rooms.known.add(OTHER)
        clip = {"frames": [dict(dt=0.0, type=Msg.ENEMY_POSE, payload=batch([entry(4), entry(6, class_index=BOSS, spawn_id=9, hp=500)], epoch=1))]}
        ctx.enemies.start_replay(SCENE, clip)
        tick(ctx, 0.1)
        ctx.link.sent.clear()
        self.assertTrue(ctx.boss.door(leave_to=OTHER))
        header, members = wire.unpack_boss(ctx.link.of(Msg.BOSS_HANDOVER)[0].payload)
        self.assertEqual((header.phase, header.count, header.alive, [m.slot for m in members]), (wire.BOSS_PREPARE, 1, 1, [6]))
        self.assertEqual(header.token >> 24, ctx.peer.slot + 1)
        deliver(ctx, Msg.BOSS_HANDOVER, wire.pack_boss(dict(token=header.token, room_epoch=2, scene=SCENE, phase=wire.BOSS_READY), []))
        self.assertEqual([wire.unpack_boss(f.payload)[0].phase for f in ctx.link.of(Msg.BOSS_HANDOVER)], [wire.BOSS_PREPARE, wire.BOSS_COMMIT])
        self.assertEqual(ctx.peer.place.scene, OTHER)

    def test_a_cancel_resumes_the_replay(self):
        ctx = make_context("host")
        pose_from_game(ctx, SCENE)
        ctx.rooms.move(SCENE)
        clip = {"frames": [dict(dt=0.0, type=Msg.ENEMY_POSE, payload=batch([entry(6, class_index=BOSS, spawn_id=9)], epoch=1))]}
        ctx.enemies.start_replay(SCENE, clip)
        tick(ctx, 0.1)
        ctx.boss.door()
        token = ctx.boss.old["token"]
        deliver(ctx, Msg.BOSS_HANDOVER, wire.pack_boss(dict(token=token, room_epoch=2, scene=SCENE, phase=wire.BOSS_CANCEL, reason=6), []))
        self.assertEqual(ctx.boss.old["stage"], "cancelled")
        self.assertFalse(ctx.enemies._paused)

    def test_no_boss_member_means_no_door(self):
        ctx = make_context("host")
        self.assertFalse(ctx.boss.door())


if __name__ == "__main__":
    unittest.main()
