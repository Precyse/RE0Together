"""Transports against a socket that plays the launcher or the game, and the link shaper."""
import queue
import random
import socket
import threading
import time
import unittest

from fake_peer import wire
from fake_peer.library import Journal
from fake_peer.link import BridgeLink, GAME_PORT, Shaper, ShaperConfig, StandinLink
from fake_peer.wire import Msg


class FakeClock:
    def __init__(self):
        self.now = 100.0

    def __call__(self):
        return self.now


class ShaperTests(unittest.TestCase):
    def test_an_idle_shaper_has_nothing_to_do(self):
        self.assertTrue(Shaper().idle())
        self.assertFalse(Shaper(ShaperConfig(delay_ms=40.0)).idle())

    def test_delay_and_jitter_move_the_send_time(self):
        clock = FakeClock()
        shaper = Shaper(ShaperConfig(delay_ms=40.0, jitter_ms=30.0), random.Random(1), clock)
        for _ in range(20):
            due = shaper.plan(reliable=False)
            self.assertTrue(clock.now + 0.04 <= due <= clock.now + 0.07)

    def test_loss_drops_unreliable_frames_only(self):
        clock = FakeClock()
        shaper = Shaper(ShaperConfig(loss=1.0), random.Random(1), clock)
        self.assertIsNone(shaper.plan(reliable=False))
        self.assertIsNotNone(shaper.plan(reliable=True))

    def test_a_cut_drops_unreliable_and_holds_reliable_in_order(self):
        clock = FakeClock()
        shaper = Shaper(clock=clock)
        shaper.cut(2.0)
        self.assertIsNone(shaper.plan(reliable=False))
        first, second = shaper.plan(reliable=True), shaper.plan(reliable=True)
        self.assertEqual(first, clock.now + 2.0)
        self.assertLessEqual(first, second)
        clock.now += 2.5
        self.assertTrue(shaper.idle())

    def test_a_stall_holds_everything_until_it_ends(self):
        clock = FakeClock()
        shaper = Shaper(clock=clock)
        shaper.stall(1.0)
        self.assertEqual(shaper.plan(reliable=False), clock.now + 1.0)
        self.assertEqual(shaper.plan(reliable=True), clock.now + 1.0)


class Launcher:
    """A socket that plays a launcher bridge: welcomes the adapter, then records what it sends."""

    def __init__(self, fake_slot=1, game_slot=0):
        self.server = socket.socket()
        self.server.bind(("127.0.0.1", 0))
        self.server.listen(1)
        self.port = self.server.getsockname()[1]
        self.fake_slot, self.game_slot = fake_slot, game_slot
        self.received = queue.Queue()
        self.conn = None
        threading.Thread(target=self._serve, daemon=True).start()

    def _serve(self):
        try:
            self.conn, _ = self.server.accept()
            self.received.put(wire.read_frame(self.conn))
            self.conn.sendall(wire.encode_frame(Msg.WELCOME, wire.welcome_payload(self.fake_slot, 0, 2, 1), slot=0))
            self.conn.sendall(wire.encode_frame(Msg.PEER_UP, wire.peer_up_payload(self.game_slot, 1, "game"), slot=0))
            while True:
                self.received.put(wire.read_frame(self.conn))
        except (OSError, ConnectionError):
            pass

    def send(self, msg_type, payload):
        self.conn.sendall(wire.encode_frame(msg_type, payload, flags=0, slot=self.game_slot))

    def close(self):
        self.server.close()
        if self.conn:
            self.conn.close()


class BridgeLinkTests(unittest.TestCase):
    def test_hello_welcome_peer_up_then_frames_both_ways(self):
        launcher = Launcher()
        self.addCleanup(launcher.close)
        got = queue.Queue()
        link = BridgeLink(launcher.port, Journal(), got.put)
        self.addCleanup(link.close)
        link.start()
        hello = launcher.received.get(timeout=3)
        self.assertEqual((hello.type, hello.payload), (Msg.HELLO, wire.hello_payload("re0")))
        deadline = time.monotonic() + 3
        while not link.session.ready and time.monotonic() < deadline:
            time.sleep(0.02)
        self.assertTrue(link.session.ready)
        self.assertEqual((link.session.fake_slot, link.session.game_slot, link.session.is_host), (1, 0, False))
        launcher.send(Msg.POSE_STATE, b"pose")
        self.assertEqual(got.get(timeout=3).payload, b"pose")
        link.send(Msg.ROOM_STATE, b"room", flags=0)
        frame = launcher.received.get(timeout=3)
        while frame.type != Msg.ROOM_STATE:  # heartbeats come first
            frame = launcher.received.get(timeout=3)
        self.assertEqual((frame.payload, frame.slot), (b"room", 0))

    def test_the_games_own_port_is_refused(self):
        with self.assertRaises(ValueError):
            BridgeLink(GAME_PORT, Journal(), lambda frame: None)

    def test_the_guard_can_refuse_a_frame(self):
        launcher = Launcher()
        self.addCleanup(launcher.close)
        link = BridgeLink(launcher.port, Journal(), lambda frame: None)
        self.addCleanup(link.close)
        link.start()
        link.guard = lambda msg_type: not wire.is_state_type(msg_type)
        self.assertFalse(link.send(Msg.POSE_STATE, b"x"))
        self.assertEqual(link.counts["refused_send"], 1)


class StandinLinkTests(unittest.TestCase):
    def test_the_game_is_welcomed_as_the_guest_and_sees_the_fake_as_the_peer(self):
        port = socket.socket()
        port.bind(("127.0.0.1", 0))
        free = port.getsockname()[1]
        port.close()
        got = queue.Queue()
        link = StandinLink(free, Journal(), got.put, fake_slot=wire.HOST_SLOT)
        self.addCleanup(link.close)
        link.start()
        game = socket.create_connection(("127.0.0.1", free), timeout=3)
        self.addCleanup(game.close)
        game.sendall(wire.encode_frame(Msg.HELLO, wire.hello_payload("re0")))
        welcome = wire.read_frame(game)
        peer_up = wire.read_frame(game)
        self.assertEqual(wire.parse_welcome(welcome.payload), {"local_slot": 1, "host_slot": 0, "max_players": 2, "epoch": 1})
        self.assertEqual((peer_up.type, peer_up.payload[0]), (Msg.PEER_UP, 0))
        game.sendall(wire.encode_frame(Msg.POSE_STATE, b"pose", flags=0, slot=0))
        self.assertEqual(got.get(timeout=3).slot, 1)  # the stand-in names the sender's slot, like the launcher does
        link.new_epoch()
        self.assertEqual(wire.parse_welcome(wire.read_frame(game).payload)["epoch"], 2)


if __name__ == "__main__":
    unittest.main()
