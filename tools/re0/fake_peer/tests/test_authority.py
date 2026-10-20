"""The authority mirror against the cases of DS2's AuthorityTable (adapters/ds2/src/authority.cpp)."""
import unittest

from fake_peer.authority import (APPLIED, CLAIM, DECLINE, DUPLICATE, INVALID_OWNER, STALE_EPOCH, STOP, UNKNOWN_OBJECT, WRONG_SENDER,
                                 Auth, AuthorityTable)
from fake_peer.wire import NO_OWNER

SCENE = 0x2D
HOST, GUEST = 0, 1


def table(local=GUEST):
    t = AuthorityTable()
    t.set_session(local, HOST)
    return t


class ClaimTests(unittest.TestCase):
    def test_an_unknown_record_starts_at_the_claims_epoch(self):
        t = table()
        self.assertEqual(t.on_message(CLAIM, HOST, Auth(SCENE, 4, HOST)), APPLIED)
        self.assertEqual((t.owner_of(SCENE), t.epoch_of(SCENE)), (HOST, 4))

    def test_a_newer_epoch_replaces_and_an_older_one_is_stale(self):
        t = table()
        t.on_message(CLAIM, HOST, Auth(SCENE, 2, HOST))
        self.assertEqual(t.on_message(CLAIM, GUEST, Auth(SCENE, 3, GUEST)), APPLIED)
        self.assertEqual(t.on_message(CLAIM, HOST, Auth(SCENE, 2, HOST)), STALE_EPOCH)
        self.assertEqual(t.owner_of(SCENE), GUEST)

    def test_the_same_epoch_from_the_same_owner_is_a_duplicate(self):
        t = table()
        t.on_message(CLAIM, HOST, Auth(SCENE, 1, HOST))
        self.assertEqual(t.on_message(CLAIM, HOST, Auth(SCENE, 1, HOST)), DUPLICATE)

    def test_two_owners_of_one_epoch_settle_on_the_lower_slot_everywhere(self):
        for order in ((HOST, GUEST), (GUEST, HOST)):
            t = table()
            for owner in order:
                t.on_message(CLAIM, owner, Auth(SCENE, 1, owner))
            self.assertEqual(t.owner_of(SCENE), HOST, order)

    def test_a_claim_for_someone_else_needs_the_host(self):
        t = table()
        self.assertEqual(t.on_message(CLAIM, GUEST, Auth(SCENE, 1, HOST)), WRONG_SENDER)
        self.assertEqual(t.on_message(CLAIM, HOST, Auth(SCENE, 1, GUEST)), APPLIED)


class StopAndDeclineTests(unittest.TestCase):
    def test_stop_must_carry_the_current_epoch_and_leaves_the_record_unowned_one_up(self):
        t = table()
        t.on_message(CLAIM, GUEST, Auth(SCENE, 1, GUEST))
        self.assertEqual(t.on_message(STOP, GUEST, Auth(SCENE, 5, GUEST)), STALE_EPOCH)
        self.assertEqual(t.on_message(STOP, GUEST, Auth(SCENE, 1, GUEST)), APPLIED)
        self.assertEqual((t.owner_of(SCENE), t.epoch_of(SCENE)), (NO_OWNER, 2))

    def test_stop_of_an_unknown_room_and_from_a_stranger(self):
        t = table()
        self.assertEqual(t.on_message(STOP, GUEST, Auth(SCENE, 1, GUEST)), UNKNOWN_OBJECT)
        t.on_message(CLAIM, HOST, Auth(SCENE, 1, HOST))
        self.assertEqual(t.on_message(STOP, GUEST, Auth(SCENE, 1, HOST)), WRONG_SENDER)

    def test_decline_bars_the_slot_until_a_voluntary_release(self):
        t = table(local=HOST)
        t.on_message(CLAIM, GUEST, Auth(SCENE, 1, GUEST))
        self.assertEqual(t.on_message(DECLINE, GUEST, Auth(SCENE, 1, GUEST)), APPLIED)
        self.assertEqual(t.on_message(CLAIM, GUEST, Auth(SCENE, 5, GUEST)), INVALID_OWNER)
        t.on_message(CLAIM, HOST, Auth(SCENE, 3, HOST))
        t.on_message(STOP, HOST, Auth(SCENE, 3, HOST))
        self.assertEqual(t.on_message(CLAIM, GUEST, Auth(SCENE, 9, GUEST)), APPLIED)


class LocalActionTests(unittest.TestCase):
    def test_claim_release_and_the_host_stop(self):
        guest, host = table(GUEST), table(HOST)
        kind, message = guest.claim(SCENE)
        self.assertEqual((kind, message), (CLAIM, Auth(SCENE, 1, GUEST)))
        self.assertIsNone(guest.claim(SCENE))
        host.on_message(CLAIM, GUEST, message)
        kind, stop = host.stop(SCENE)
        self.assertEqual((kind, stop.owner), (STOP, GUEST))
        kind, release = guest.release(SCENE)
        self.assertEqual((kind, release.epoch), (STOP, 1))

    def test_a_guest_cannot_take_a_room_someone_else_owns(self):
        t = table(GUEST)
        t.on_message(CLAIM, HOST, Auth(SCENE, 1, HOST))
        self.assertIsNone(t.claim(SCENE))

    def test_a_slot_that_leaves_loses_its_rooms(self):
        t = table()
        t.on_message(CLAIM, HOST, Auth(SCENE, 1, HOST))
        t.drop_slot(HOST)
        self.assertEqual((t.owner_of(SCENE), t.epoch_of(SCENE)), (NO_OWNER, 2))

    def test_claims_of_a_slot_are_repeated_for_a_new_peer(self):
        t = table()
        t.on_message(CLAIM, HOST, Auth(SCENE, 4, HOST))
        self.assertEqual(t.claims_of(HOST), [Auth(SCENE, 4, HOST)])


if __name__ == "__main__":
    unittest.main()
