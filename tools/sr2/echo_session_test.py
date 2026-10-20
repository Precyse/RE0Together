"""Tests of echo_session.py: the port of launcher/SlotAssigner.cs and Session.cs, and the relay decision. The expected frames
are what Session.cs sends for the same lobby changes (ApplyMembership, Reset, UpdatePeers, End)."""
import unittest
from unittest import mock

import echo_session as es
import wire

COUNT = 4
MAX_PLAYERS = 4
SECOND_EPOCH = 2
THIRD_EPOCH = 3


def sid(instance):
    return es.steam_id(instance)


def up(slot, instance):
    return es.PeerUp(slot, sid(instance), f"Echo {instance}")


def welcome(instance, slot, epoch):
    return es.Welcome(slot, es.HOST_SLOT, MAX_PLAYERS, epoch, sid(instance))


def linked_views(count=COUNT, max_players=MAX_PLAYERS):
    views = es.Views(count, max_players)
    for k in range(1, count + 1):
        views.hello(k)
    return views


class AssignSlotsTest(unittest.TestCase):
    def test_host_first_then_ascending(self):
        self.assertEqual(es.assign_slots(5, [9, 3, 5, 7, 5], 4), {5: 0, 3: 1, 7: 2, 9: 3})

    def test_extras_get_no_slot(self):
        self.assertEqual(es.assign_slots(5, [9, 3, 5, 7], 3), {5: 0, 3: 1, 7: 2})

    def test_host_need_not_be_lowest(self):
        self.assertEqual(es.assign_slots(9, [3, 5, 9], 4), {9: 0, 3: 1, 5: 2})

    def test_host_alone(self):
        self.assertEqual(es.assign_slots(5, [], 4), {5: 0})


class AddressTest(unittest.TestCase):
    def test_the_fixed_ids_have_distinct_addresses(self):
        addresses = {wire.virtual_address(sid(k)) for k in range(1, es.MAX_INSTANCES + 1)}
        self.assertEqual(len(addresses), es.MAX_INSTANCES)

    def test_a_shared_address_is_refused(self):
        with mock.patch.object(es.wire, "virtual_address", return_value="10.0.0.1"):
            with self.assertRaises(ValueError):
                es.assert_distinct_addresses(2)

    def test_instance_count_is_bounded(self):
        with self.assertRaises(ValueError):
            es.Views(es.MAX_INSTANCES + 1, MAX_PLAYERS)


class JoinTest(unittest.TestCase):
    def test_four_instances_get_slots_one_less_than_their_number(self):
        views = es.Views(COUNT, MAX_PLAYERS)
        self.assertEqual([views.slot_of(k) for k in range(1, COUNT + 1)], [0, 1, 2, 3])
        self.assertTrue(all(view.epoch == es.FIRST_EPOCH for view in views.views.values()))

    def test_hello_answers_with_the_snapshot_of_that_view(self):
        views = es.Views(COUNT, MAX_PLAYERS)
        self.assertEqual(views.hello(3), [welcome(3, 2, 1), up(0, 1), up(1, 2), up(3, 4)])

    def test_nothing_is_sent_to_a_game_that_has_not_said_hello(self):
        views = es.Views(COUNT, MAX_PLAYERS)
        self.assertEqual(views.leave(2)[1], [])

    def test_more_instances_than_players_leave_the_last_without_a_slot(self):
        views = linked_views(COUNT, 3)
        self.assertIsNone(views.slot_of(4))
        self.assertEqual(views.slot_of(3), 2)

    def test_the_snapshot_repeats_after_a_reconnect(self):
        views = linked_views()
        views.unlinked(2)
        self.assertEqual(views.hello(2), [welcome(2, 1, 1), up(0, 1), up(2, 3), up(3, 4)])


class LeaveTest(unittest.TestCase):
    def test_the_lowest_guest_leaving_reslots_the_others(self):
        events = linked_views().leave(2)
        self.assertEqual(events[1], [es.PeerDown(1), es.PeerDown(2), es.PeerDown(3), up(1, 3), up(2, 4)])
        self.assertEqual(events[3], [welcome(3, 1, SECOND_EPOCH), up(0, 1), up(2, 4)])
        self.assertEqual(events[4], [welcome(4, 2, SECOND_EPOCH), up(0, 1), up(1, 3)])

    def test_the_host_keeps_its_epoch_and_the_moved_guests_get_the_next(self):
        views = linked_views()
        views.leave(2)
        self.assertEqual([views.views[k].epoch for k in (1, 3, 4)], [1, SECOND_EPOCH, SECOND_EPOCH])
        self.assertEqual([views.slot_of(k) for k in (1, 2, 3, 4)], [0, None, 1, 2])

    def test_the_leaver_ends_its_own_session(self):
        views = linked_views()
        events = views.leave(2)
        self.assertEqual(events[2], [es.PeerDown(0), es.PeerDown(2), es.PeerDown(3)])
        self.assertTrue(views.views[2].ended)
        self.assertEqual(views.last_slot(2), 1)

    def test_the_last_guest_leaving_moves_nobody(self):
        views = linked_views()
        events = views.leave(4)
        self.assertEqual(events[1], [es.PeerDown(3)])
        self.assertEqual(events[2], [es.PeerDown(3)])
        self.assertEqual(events[3], [es.PeerDown(3)])
        self.assertEqual(views.views[2].epoch, 1)


class BackTest(unittest.TestCase):
    def test_a_returning_guest_restarts_and_the_others_reslot(self):
        views = linked_views()
        views.leave(2)
        events = views.back(2)
        self.assertEqual(events[2], [welcome(2, 1, 1), up(0, 1), up(2, 3), up(3, 4)])
        self.assertEqual(events[1], [es.PeerDown(1), es.PeerDown(2), up(1, 2), up(2, 3), up(3, 4)])
        self.assertEqual(events[3], [welcome(3, 2, THIRD_EPOCH), up(0, 1), up(1, 2), up(3, 4)])
        self.assertEqual(events[4], [welcome(4, 3, THIRD_EPOCH), up(0, 1), up(1, 2), up(2, 3)])


class HostLeavesTest(unittest.TestCase):
    def test_every_session_ends(self):
        views = linked_views()
        events = views.leave(1)
        self.assertEqual(events[1], [es.PeerDown(1), es.PeerDown(2), es.PeerDown(3)])
        self.assertEqual(events[2], [es.PeerDown(0), es.PeerDown(2), es.PeerDown(3)])
        self.assertTrue(all(view.ended and view.epoch == SECOND_EPOCH for view in views.views.values()))

    def test_datagrams_have_no_route_afterwards(self):
        views = linked_views()
        views.leave(1)
        self.assertEqual(views.route(2, wire.SLOT_ALL), es.Route((), (es.NO_SLOT,)))

    def test_the_host_returning_restarts_every_session(self):
        views = linked_views()
        views.leave(1)
        events = views.back(1)
        self.assertEqual(events[2], [welcome(2, 1, 1), up(0, 1), up(2, 3), up(3, 4)])
        self.assertEqual(events[1], [welcome(1, 0, 1), up(1, 2), up(2, 3), up(3, 4)])
        self.assertEqual(views.route(2, wire.SLOT_ALL).deliveries, ((1, 1), (3, 1), (4, 1)))


class RouteTest(unittest.TestCase):
    def test_a_unicast_carries_the_senders_slot(self):
        views = linked_views()
        self.assertEqual(views.route(1, 2).deliveries, ((3, 0),))
        self.assertEqual(views.route(2, 2).deliveries, ((3, 1),))

    def test_a_broadcast_reaches_every_other_member(self):
        views = linked_views()
        self.assertEqual(views.route(1, wire.SLOT_ALL).deliveries, ((2, 0), (3, 0), (4, 0)))
        self.assertEqual(views.route(3, wire.SLOT_ALL).deliveries, ((1, 2), (2, 2), (4, 2)))

    def test_traffic_is_counted_per_instance(self):
        views = linked_views()
        views.route(1, wire.SLOT_ALL)
        self.assertEqual((views.traffic[1].sent, views.traffic[2].received, views.traffic[2].sent), (3, 1, 0))

    def test_datagrams_follow_the_ids_after_a_reslot(self):
        views = linked_views()
        views.leave(2)
        self.assertEqual(views.route(1, 1).deliveries, ((3, 0),))
        self.assertEqual(views.route(4, 1).deliveries, ((3, 2),))

    def test_a_slot_without_a_member_is_counted(self):
        views = linked_views(3)
        self.assertEqual(views.route(1, 3), es.Route((), (es.NO_MEMBER,)))
        self.assertEqual(views.drops[es.NO_MEMBER], 1)

    def test_a_sender_without_a_slot_is_counted(self):
        views = linked_views()
        views.leave(2)
        self.assertEqual(views.route(2, 0), es.Route((), (es.NO_SLOT,)))
        self.assertEqual(views.drops[es.NO_SLOT], 1)

    def test_a_game_that_is_not_linked_is_counted(self):
        views = es.Views(COUNT, MAX_PLAYERS)
        views.hello(1)
        self.assertEqual(views.route(1, 3), es.Route((), (es.NOT_LINKED,)))
        self.assertEqual(views.route(1, wire.SLOT_ALL).drops, (es.NOT_LINKED,) * 3)

    def test_a_dropped_link_loses_frames_both_ways_and_restores(self):
        views = linked_views()
        views.drop_link(3)
        self.assertEqual(views.route(1, 2), es.Route((), (es.DROPPED_LINK,)))
        self.assertEqual(views.route(3, wire.SLOT_ALL), es.Route((), (es.DROPPED_LINK,)))
        self.assertEqual(views.route(1, wire.SLOT_ALL).deliveries, ((2, 0), (4, 0)))
        views.restore_link(3)
        self.assertEqual(views.route(1, 2).deliveries, ((3, 0),))
        self.assertEqual(views.drops[es.DROPPED_LINK], 3)

    def test_clearing_the_counters(self):
        views = linked_views()
        views.route(1, COUNT)
        views.clear_counters()
        self.assertEqual(sum(views.drops.values()), 0)


if __name__ == "__main__":
    unittest.main()
