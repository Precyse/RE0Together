"""Transports against a socket that plays the launcher or the game, with and without a shaped link."""
import queue
import socket
import threading
import time
import unittest

from fake_peer import wire
from fake_peer.library import Journal
from fake_peer.impair import make_params
from fake_peer.link import BridgeLink, GAME_PORT, StandinLink
from fake_peer.shaper import IN, Shaper
from fake_peer.wire import Msg


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

    def test_the_inbound_shaper_drops_unreliable_and_delays_reliable_frames_from_the_game(self):
        launcher = Launcher()
        self.addCleanup(launcher.close)
        got = queue.Queue()
        shaper = Shaper(make_params(loss=1.0, latency_ms=150.0), seed=5)
        link = BridgeLink(launcher.port, Journal(), got.put, shaper)
        self.addCleanup(link.close)
        link.start()
        deadline = time.monotonic() + 3
        while not link.session.ready and time.monotonic() < deadline:
            time.sleep(0.02)
        launcher.send(Msg.POSE_STATE, b"lost")
        launcher.conn.sendall(wire.encode_frame(Msg.ROOM_STATE, b"kept", flags=wire.FLAG_RELIABLE, slot=0))
        started = time.monotonic()
        self.assertEqual(got.get(timeout=3).payload, b"kept")
        self.assertGreaterEqual(time.monotonic() - started, 0.1)
        self.assertTrue(got.empty())
        self.assertEqual(link.counts["shaped_drop_in"], 1)

    def test_a_one_way_cut_on_the_inbound_side_leaves_the_outgoing_frames_alone(self):
        launcher = Launcher()
        self.addCleanup(launcher.close)
        shaper = Shaper()
        shaper.cut(30.0, IN)
        link = BridgeLink(launcher.port, Journal(), lambda frame: None, shaper)
        self.addCleanup(link.close)
        link.start()
        link.send(Msg.ROOM_STATE, b"out", flags=0)
        frame = launcher.received.get(timeout=3)
        while frame.type != Msg.ROOM_STATE:
            frame = launcher.received.get(timeout=3)
        self.assertEqual(frame.payload, b"out")

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
