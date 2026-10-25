"""The world digest: the fake's checksums (pinned to the adapter's values), the digest module's sends, and the T7 digest gates
on synthetic logs."""
import unittest

from fake_peer import wire, world
from fake_peer.gates import FAIL, MISSING, PASS, Evidence, Gate
from fake_peer.library import IN, Journal
from fake_peer.logwatch import LogStore
from fake_peer.scenarios import t7_flags
from fake_peer.tests.support import deliver, make_context, tick
from fake_peer.wire import Frame, Msg

# adapters/re0/tests/world_digest_test.cpp pins the same values for the same bits and rooms.
KNOWN_BITS = (0x000, 0x040, 0x037, 0x312, 0x412, 0x612, 0x7E1)
KNOWN_CRCS = (0x26B68855, 0x390A7F35, 0xE58EFF0C)
KNOWN_ROOMS = ((0x0A05, 0, 2), (0x0102, 1, 5), (0x0B01, 0, 1))
KNOWN_AUTHORITY_HASH = 0xE99D86DE
SCENE = 0x2D


class Room:
    def __init__(self, scene, owner, epoch):
        self.id, self.owner, self.epoch = scene, owner, epoch


def words_with(*bits):
    words = [0] * wire.FLAG_WORDS
    for bit in bits:
        words[bit // 32] |= 1 << bit % 32
    return words


class ChecksumTests(unittest.TestCase):
    def test_the_flag_crcs_of_a_known_word_set_match_the_adapter(self):
        self.assertEqual(world.flag_crcs(words_with(*KNOWN_BITS)), KNOWN_CRCS)

    def test_presence_flags_do_not_change_any_crc(self):
        base = world.flag_crcs(words_with())
        for bit in world.PRESENCE_FLAGS:
            self.assertEqual(world.flag_crcs(words_with(bit)), base, hex(bit))

    def test_each_family_changes_only_its_own_crc(self):
        base = world.flag_crcs(words_with())
        for bit, changed in ((0x100, 0), (0x312, 1), (0x411, 1), (0x412, 0), (0x612, 2), (0x711, 2), (0x712, 0)):
            crcs = world.flag_crcs(words_with(bit))
            self.assertEqual([i for i in range(3) if crcs[i] != base[i]], [changed], hex(bit))

    def test_the_authority_hash_matches_the_adapter_and_ignores_order_and_unowned_rooms(self):
        rooms = [Room(*room) for room in KNOWN_ROOMS]
        self.assertEqual(world.authority_hash(rooms), KNOWN_AUTHORITY_HASH)
        self.assertEqual(world.authority_hash(reversed(rooms)), KNOWN_AUTHORITY_HASH)
        self.assertEqual(world.authority_hash(rooms + [Room(0x0C01, wire.NO_OWNER, 9)]), KNOWN_AUTHORITY_HASH)
        self.assertNotEqual(world.authority_hash(rooms[:2]), KNOWN_AUTHORITY_HASH)


class DigestModuleTests(unittest.TestCase):
    def setUp(self):
        self.ctx = make_context("host")
        self.ctx.flags.words = words_with(*KNOWN_BITS)
        self.ctx.flags.serial = 7
        self.ctx.peer.place.scene = SCENE
        self.digest = self.ctx.digest
        self.link = self.ctx.link

    def sent(self):
        return [wire.WorldDigest.unpack(f.payload) for f in self.link.of(Msg.WORLD_DIGEST)]

    def test_nothing_is_sent_before_start(self):
        tick(self.ctx)
        tick(self.ctx, 5.0)
        self.assertEqual(self.sent(), [])

    def test_the_digest_is_built_from_the_mirrors(self):
        self.digest.start()
        tick(self.ctx)
        (digest,) = self.sent()
        self.assertEqual((digest.flag_crc, digest.enemy_flag_crc, digest.act_flag_crc), KNOWN_CRCS)
        self.assertEqual((digest.flag_serial, digest.pending_proposals, digest.flags, digest.scene),
                         (7, 0, wire.DIGEST_CAUGHT_UP, SCENE))
        self.assertEqual(digest.authority_hash, world.authority_hash([]))
        self.assertEqual(self.link.of(Msg.WORLD_DIGEST)[0].flags, 0)

    def test_it_goes_out_once_a_second(self):
        self.digest.start()
        tick(self.ctx)
        tick(self.ctx, 0.5)
        self.assertEqual(len(self.sent()), 1)
        tick(self.ctx, 1.5)
        self.assertEqual(len(self.sent()), 2)
        self.assertEqual(self.digest.sent, 2)

    def test_the_authority_mirror_and_pending_proposals_are_in_it(self):
        self.ctx.authority.table.records[SCENE] = Room(SCENE, 1, 3)
        self.ctx.flags.proposed.append([])
        self.digest.start()
        tick(self.ctx)
        (digest,) = self.sent()
        self.assertEqual(digest.authority_hash, world.authority_hash([Room(SCENE, 1, 3)]))
        self.assertEqual(digest.pending_proposals, 1)

    def test_the_load_serial_is_the_games_own(self):
        deliver(self.ctx, Msg.WORLD_DIGEST, wire.WorldDigest.pack(load_serial=9, flags=wire.DIGEST_CAUGHT_UP))
        self.assertEqual(self.digest.received, 1)
        self.digest.start()
        tick(self.ctx)
        (digest,) = self.sent()
        self.assertEqual((digest.reserved, digest.load_serial), (0, 9))

    def test_a_flip_changes_the_enemy_crc_only_and_leaves_the_mirror_alone(self):
        self.digest.start()
        self.digest.flip()
        tick(self.ctx)
        (digest,) = self.sent()
        self.assertEqual((digest.flag_crc, digest.act_flag_crc), (KNOWN_CRCS[0], KNOWN_CRCS[2]))
        self.assertNotEqual(digest.enemy_flag_crc, KNOWN_CRCS[1])
        self.assertEqual(self.ctx.flags.words, words_with(*KNOWN_BITS))
        self.digest.stop()
        self.assertIsNone(self.digest.flip_bit)


FLIP_MARK_MS = 100_000.0
EQUAL_MARK_MS, EQUAL_END_MS = 50_000.0, 60_000.0
MARKS = {"digest_equal": EQUAL_MARK_MS, "digest_equal_end": EQUAL_END_MS, "digest_flip": FLIP_MARK_MS}
DIVERGED = "world_digest: diverged (enemy flags) at serial 7"


class FakeDigest:
    def __init__(self, sent=5, received=5):
        self.sent, self.received = sent, received


def evidence(lines=(), frames=(), marks=MARKS, digest=None):
    store = LogStore()
    for t, text in lines:
        store.add("adapter", text, t)
    journal = Journal()
    for t, msg_type in frames:
        journal.frame(IN, Frame(int(msg_type), 0, 1, b""), t=t)
    return Evidence(store, journal, marks, {"digest": digest or FakeDigest()})


def judge(seat, gate_id, ev):
    gate = next(g for g in t7_flags.digest_gates(seat) if g.id == gate_id)
    return gate.judge(ev)


class DigestGateTests(unittest.TestCase):
    def test_equal_digests_pass_only_when_nothing_diverged_in_the_window(self):
        self.assertEqual(judge("host", "T7.digest-equal", evidence()).verdict, PASS)
        inside = evidence(lines=[(EQUAL_MARK_MS + 5000, DIVERGED)])
        self.assertEqual(judge("host", "T7.digest-equal", inside).verdict, FAIL)
        after = evidence(lines=[(FLIP_MARK_MS + 3000, DIVERGED)])
        self.assertEqual(judge("host", "T7.digest-equal", after).verdict, PASS)

    def test_equal_digests_need_digests_in_both_directions(self):
        self.assertEqual(judge("guest", "T7.digest-equal", evidence(digest=FakeDigest(received=0))).verdict, MISSING)
        self.assertEqual(judge("guest", "T7.digest-equal", evidence(digest=FakeDigest(sent=0))).verdict, MISSING)

    def test_the_flip_needs_the_divergence_line_and_a_request_within_two_seconds(self):
        reported = FLIP_MARK_MS + 3000
        line = [(reported, DIVERGED)]
        self.assertEqual(judge("host", "T7.digest-flip", evidence()).verdict, MISSING)
        self.assertEqual(judge("host", "T7.digest-flip", evidence(line, [(reported + 500.0, Msg.SNAPSHOT_REQUEST)])).verdict, PASS)
        self.assertEqual(judge("host", "T7.digest-flip", evidence(line, [(reported + 2500.0, Msg.SNAPSHOT_REQUEST)])).verdict, FAIL)
        self.assertEqual(judge("host", "T7.digest-flip", evidence(line)).verdict, FAIL)

    def test_a_game_that_is_the_host_answers_with_resync_request_not_snapshot_request(self):
        reported = FLIP_MARK_MS + 3000
        line = [(reported, DIVERGED)]
        self.assertEqual(judge("guest", "T7.digest-flip", evidence(line, [(reported + 500.0, Msg.RESYNC_REQUEST)])).verdict, PASS)
        self.assertEqual(judge("guest", "T7.digest-flip", evidence(line, [(reported + 500.0, Msg.SNAPSHOT_REQUEST)])).verdict, FAIL)

    def test_other_divergence_scopes_do_not_satisfy_the_flip_gate(self):
        other = evidence([(FLIP_MARK_MS + 3000, "world_digest: diverged (act flags) at serial 7")],
                         [(FLIP_MARK_MS + 3500, Msg.SNAPSHOT_REQUEST)])
        self.assertEqual(judge("host", "T7.digest-flip", other).verdict, MISSING)

    def test_both_seats_carry_both_digest_gates_after_the_flag_gates(self):
        for seat in t7_flags.SEATS:
            ids = [g.id for g in t7_flags.gates(seat)]
            self.assertEqual(ids[-2:], ["T7.digest-equal", "T7.digest-flip"])
            self.assertTrue(all(isinstance(g, Gate) for g in t7_flags.gates(seat)))


if __name__ == "__main__":
    unittest.main()
