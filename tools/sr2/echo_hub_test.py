"""End-to-end test of echo_hub.py without the game: four stand-in adapters (tunnel_launcher_test.Adapter, a socket that says
HELLO and collects frames) connect to a hub in --identify order mode. Checks the snapshots, broadcast and unicast relay with
the sender's slot, a re-slot after `leave` with datagrams following, `back`, link faults, the join and status files, the
identity by PID and exe folder, and the launch plan."""
import os
import shutil
import socket
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path

import echo_files
import echo_hub
import echo_launch
import echo_session as es
import wire
from echo_identity import normal_path
from tunnel_launcher_test import Adapter, wait_for

COUNT = 4
SETTLE_S = 0.3
WAIT_S = 10
LOG_STAMP = "2026-10-20 12:00:00.000 "


def silent(*_args):
    pass


def welcomes(adapter):
    with adapter.lock:
        return [wire.WELCOME.unpack(f[3]) for f in adapter.frames if f[0] == wire.MSG_WELCOME]


def peer_ups(adapter):
    with adapter.lock:
        return [wire.PEER_UP_FIXED.unpack(f[3][:wire.PEER_UP_FIXED.size])[:2] for f in adapter.frames if f[0] == wire.MSG_PEER_UP]


def datagram_from(adapter, slot, data):
    return adapter.find(wire.MSG_GAME_DATAGRAM, lambda s, payload: s == slot and payload[wire.DST.size:] == data)


def wait_welcome(adapter, epoch):
    return adapter.wait(wire.MSG_WELCOME, lambda s, payload: wire.WELCOME.unpack(payload)[3] == epoch)


class HubTest(unittest.TestCase):
    def setUp(self):
        self.work = Path(tempfile.mkdtemp(prefix="cf_echo_hub_"))
        self.folders = {k: str(self.work / f"inst{k}") for k in range(1, COUNT + 1)}
        for folder in self.folders.values():
            os.makedirs(os.path.join(folder, echo_files.COOP_DIR))
        self.hub = echo_hub.Hub(self.folders, port=0, identify=echo_hub.IDENTIFY_ORDER, out=silent)
        self.hub.start()
        self.adapters = {}
        for k in range(1, COUNT + 1):
            self.adapters[k] = Adapter(self.hub.port)
            self.assertTrue(self.adapters[k].wait(wire.MSG_WELCOME))
            self.assertTrue(self.hub.hello_events[k].wait(WAIT_S))

    def tearDown(self):
        for adapter in self.adapters.values():
            adapter.close()
        self.hub.stop()
        shutil.rmtree(self.work, ignore_errors=True)

    def settle(self):
        time.sleep(SETTLE_S)

    def test_every_instance_gets_its_welcome_and_peers(self):
        for k, adapter in self.adapters.items():
            self.assertEqual(welcomes(adapter), [(k - 1, 0, 4, 1, es.steam_id(k))])
            others = [(other - 1, es.steam_id(other)) for other in range(1, COUNT + 1) if other != k]
            self.assertEqual(peer_ups(adapter), others)

    def test_a_host_broadcast_reaches_the_three_guests_with_slot_zero(self):
        self.adapters[1].send_datagram(wire.SLOT_ALL, wire.BROADCAST, b"from the host")
        for k in (2, 3, 4):
            self.assertIsNotNone(self.adapters[k].wait(wire.MSG_GAME_DATAGRAM, lambda s, p: s == 0 and p.endswith(b"from the host")))
        self.settle()
        self.assertIsNone(datagram_from(self.adapters[1], 0, b"from the host"))

    def test_a_guest_unicast_to_a_guest_arrives_with_the_senders_slot_only_there(self):
        self.adapters[2].send_datagram(2, 0x0A010203, b"guest to guest")
        self.assertIsNotNone(self.adapters[3].wait(wire.MSG_GAME_DATAGRAM, lambda s, p: s == 1 and p.endswith(b"guest to guest")))
        self.settle()
        for k in (1, 2, 4):
            self.assertIsNone(self.adapters[k].find(wire.MSG_GAME_DATAGRAM, lambda s, p: p.endswith(b"guest to guest")))

    def test_leave_reslots_the_others_and_datagrams_follow(self):
        self.assertIn("done", self.hub.command("leave 2"))
        self.assertTrue(wait_welcome(self.adapters[3], 2))
        self.assertTrue(wait_welcome(self.adapters[4], 2))
        self.assertEqual(welcomes(self.adapters[3])[-1][0], 1)
        self.assertEqual(welcomes(self.adapters[4])[-1][0], 2)
        self.assertTrue(self.adapters[1].wait(wire.MSG_PEER_DOWN, lambda s, p: p == bytes([3])))
        self.adapters[4].send_datagram(wire.SLOT_ALL, wire.BROADCAST, b"after the leave")
        for k in (1, 3):
            self.assertIsNotNone(self.adapters[k].wait(wire.MSG_GAME_DATAGRAM, lambda s, p: s == 2 and p.endswith(b"after the leave")))
        self.settle()
        self.assertIsNone(datagram_from(self.adapters[2], 2, b"after the leave"))
        self.adapters[2].send_datagram(wire.SLOT_ALL, wire.BROADCAST, b"from the leaver")
        self.settle()
        self.assertIsNone(datagram_from(self.adapters[1], 1, b"from the leaver"))
        self.assertGreaterEqual(self.hub.views.drops[es.NO_SLOT], 1)
        self.assertFalse(os.path.exists(echo_files.coop_path(self.folders[2], echo_files.JOIN_FILE)))

    def test_back_restores_the_slots(self):
        self.hub.command("leave 2")
        self.hub.command("back 2")
        self.assertTrue(wait_welcome(self.adapters[3], 3))
        self.assertEqual(welcomes(self.adapters[3])[-1][0], 2)
        self.assertTrue(wait_for(lambda: len(welcomes(self.adapters[2])) == 2, WAIT_S))
        self.assertEqual(welcomes(self.adapters[2])[-1][:4], (1, 0, 4, 1))
        self.assertTrue(os.path.exists(echo_files.coop_path(self.folders[2], echo_files.JOIN_FILE)))

    def test_drop_and_restore(self):
        self.hub.command("drop 3")
        self.adapters[1].send_datagram(2, 0x0A010203, b"into the outage")
        self.settle()
        self.assertIsNone(datagram_from(self.adapters[3], 0, b"into the outage"))
        self.assertEqual(self.hub.views.drops[es.DROPPED_LINK], 1)
        self.hub.command("restore 3")
        self.adapters[1].send_datagram(2, 0x0A010203, b"after the outage")
        self.assertIsNotNone(self.adapters[3].wait(wire.MSG_GAME_DATAGRAM, lambda s, p: p.endswith(b"after the outage")))

    def test_join_files_are_written_and_removed(self):
        host = Path(echo_files.coop_path(self.folders[1], echo_files.JOIN_FILE)).read_text()
        guest = Path(echo_files.coop_path(self.folders[3], echo_files.JOIN_FILE)).read_text()
        self.assertEqual(host, f"role=host\ntoken={echo_files.TOKEN}\npassword={echo_files.PASSWORD}\nname=Echo 1\n")
        self.assertEqual(guest, f"role=guest\ntoken={echo_files.TOKEN}\npassword={echo_files.PASSWORD}\nname=Echo 3\n")
        self.hub.command("rejoin 3")
        self.assertEqual(Path(echo_files.coop_path(self.folders[3], echo_files.JOIN_FILE)).read_text(), guest)
        self.hub.command("quit")
        for folder in self.folders.values():
            self.assertFalse(os.path.exists(echo_files.coop_path(folder, echo_files.JOIN_FILE)))

    def test_status_files_are_polled_and_shown(self):
        with open(echo_files.coop_path(self.folders[2], echo_files.STATUS_FILE), "w") as status:
            status.write("state=dialog\n")
        self.hub.poll_status()
        self.assertIn("status ''", self.hub.status().splitlines()[1])
        self.assertIn("status 'state=dialog'", self.hub.status().splitlines()[2])

    def test_status_shows_the_link_slot_and_counts(self):
        self.adapters[1].send_datagram(wire.SLOT_ALL, wire.BROADCAST, b"counted")
        self.assertIsNotNone(self.adapters[4].wait(wire.MSG_GAME_DATAGRAM, lambda s, p: p.endswith(b"counted")))
        rows = self.hub.status().splitlines()
        self.assertIn("link up", rows[1])
        self.assertIn("datagrams in 0 out 3", rows[1])
        self.assertIn("slot 3", rows[4])

    def test_a_check_reads_the_instance_logs_after_the_mark_only(self):
        for k, folder in self.folders.items():
            with open(echo_files.coop_path(folder, echo_files.LOG_FILE), "w") as log:
                log.write(LOG_STAMP + f"tunnel: steam transport, launcher port {self.hub.port}\n")
                log.write(LOG_STAMP + f"tunnel: own address {self.hub.address(k)}\n")
                log.write(LOG_STAMP + "game socket: connection reset reports off\n")
        answer = self.hub.command("check link")
        self.assertIn(f"{4 * 3} passed, 0 failed", answer)
        self.hub.command("mark")
        self.assertEqual(self.hub.tails[1].lines(), [])

    def test_bad_commands_are_reported(self):
        self.assertIn("error", self.hub.command("leave 9"))
        self.assertIn("error", self.hub.command("check nothing"))
        self.assertIn("unknown command", self.hub.command("jump"))
        self.assertEqual(self.hub.command(""), "")

    def test_a_delay_holds_frames_in_order(self):
        self.hub.delay_s = 0.2
        started = time.monotonic()
        self.adapters[1].send_datagram(2, 0x0A010203, b"first")
        self.adapters[1].send_datagram(2, 0x0A010203, b"second")
        self.assertIsNotNone(self.adapters[3].wait(wire.MSG_GAME_DATAGRAM, lambda s, p: p.endswith(b"second")))
        self.assertGreaterEqual(time.monotonic() - started, 0.2)
        with self.adapters[3].lock:
            order = [f[3][wire.DST.size:] for f in self.adapters[3].frames if f[0] == wire.MSG_GAME_DATAGRAM]
        self.assertEqual(order, [b"first", b"second"])


class RefusalTest(unittest.TestCase):
    def test_a_taken_port_is_refused(self):
        taken = socket.socket()
        taken.bind((echo_hub.LISTEN_HOST, 0))
        taken.listen()
        work = tempfile.mkdtemp(prefix="cf_echo_port_")
        try:
            hub = echo_hub.Hub({1: work}, port=taken.getsockname()[1], out=silent)
            with self.assertRaises(echo_hub.HubError):
                hub.start()
        finally:
            taken.close()
            shutil.rmtree(work, ignore_errors=True)


class IdentityTest(unittest.TestCase):
    def setUp(self):
        self.work = Path(tempfile.mkdtemp(prefix="cf_echo_identity_"))

    def tearDown(self):
        shutil.rmtree(self.work, ignore_errors=True)

    def test_a_connection_is_identified_by_the_folder_of_its_exe(self):
        hub = echo_hub.Hub({1: str(Path(sys.executable).parent)}, port=0, out=silent)
        listener = socket.socket()
        listener.bind((echo_hub.LISTEN_HOST, 0))
        listener.listen()
        hub.port = listener.getsockname()[1]
        client = socket.create_connection((echo_hub.LISTEN_HOST, hub.port))
        server, _ = listener.accept()
        try:
            instance, pid = hub.identify(server)
        finally:
            client.close()
            server.close()
            listener.close()
        self.assertEqual((instance, pid), (1, os.getpid()))

    def test_a_connection_from_another_folder_is_closed(self):
        hub = echo_hub.Hub({1: str(self.work)}, port=0, out=silent)
        hub.start()
        client = socket.create_connection((echo_hub.LISTEN_HOST, hub.port))
        try:
            client.sendall(wire.hello("sr2"))
            client.settimeout(WAIT_S)
            try:
                self.assertEqual(client.recv(1), b"")
            except ConnectionResetError:
                pass
        finally:
            client.close()
            hub.stop()


class LaunchTest(unittest.TestCase):
    def test_only_instances_that_are_not_running_start_host_first(self):
        folders = {3: r"C:\echo\guest3", 1: r"C:\game", 2: r"C:\echo\guest2"}
        running = {normal_path(echo_launch.exe_of(folders[2]))}
        self.assertEqual(echo_launch.instances_to_start(folders, running), [1, 3])

    def test_each_start_waits_for_the_previous_hello(self):
        folders = {1: r"C:\game", 2: r"C:\echo\guest2", 3: r"C:\echo\guest3"}
        events = {k: threading.Event() for k in folders}
        started = []

        def start(folder):
            started.append(folder)
            events[len(started)].set()

        echo_launch.launch(folders, events, silent, start=start, running=lambda: set())
        self.assertEqual(started, [folders[1], folders[2], folders[3]])

    def test_a_missing_exe_is_reported_and_the_rest_start(self):
        folders = {1: r"C:\game", 2: r"C:\echo\guest2"}
        events = {k: threading.Event() for k in folders}
        events[2].set()
        said = []

        def start(folder):
            if folder == folders[1]:
                raise FileNotFoundError("no exe")
            said.append(folder)

        echo_launch.launch(folders, events, said.append, start=start, running=lambda: set())
        self.assertIn(folders[2], said)
        self.assertTrue(any("cannot start instance 1" in line for line in said))


if __name__ == "__main__":
    unittest.main()
