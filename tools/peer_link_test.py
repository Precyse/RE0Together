"""Peer link hardening on local transport: a host launcher with fake_adapter.py as its game, and a fake guest launcher (UDP,
like spoof_peer.py) that stops its presence for a while or floods frames. No real game, no Steam.

  --gap S    the peer is silent for S seconds, then comes back. The local lobby drops a silent peer after 3 s, the host then
             holds it for 10 s. S=5: the adapter prints no PEER_DOWN and a second PEER_UP for the same slot, and game frames
             keep reaching the peer during the gap. S=15: the adapter prints PEER_DOWN.
  --flood N  the peer sends N unreliable 0x0100 frames a second for 5 s. With 2000/s the launcher logs a "Peer budget:"
             drop line while the session and the adapter link stay up (no PEER_DOWN, no "link down").

Usage: python tools/peer_link_test.py --gap 5   (build the launcher into launcher/bin/Check first; COOP_LAUNCHER overrides the exe)"""
import argparse
import os
import re
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from save_sync_test import EXIT_TIMEOUT_S, ROOT, TRANSFER_TIMEOUT_S, launcher, wait_for

EXE = Path(os.environ.get("COOP_LAUNCHER", ROOT / "launcher" / "bin" / "Check" / "coop-launcher.exe"))
LAUNCHER_PORT = 27971
PEER_PORT = 27972
BRIDGE_PORT = 27973
PRESENCE_TYPE = 0x0030
GAME_TYPE = 0x0100
PRESENCE_TIMEOUT_S = 3
GRACE_S = 10
WARMUP_S = 4
RETURN_S = 4
FLOOD_S = 5
BUDGET_REPORT_S = 5
BATCH_S = 0.01
TICK_S = 1.0
SLACK_S = 1.5
PEER_UP = re.compile(r"PEER_UP slot=1\b")


def wire(msg_type, flags=0, slot=1, payload=b""):
    return struct.pack("<HBB", msg_type, flags, slot) + payload


class FakeGuest:
    """The guest launcher's side of the local transport: presence datagrams and game frames to the host launcher."""

    def __init__(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("127.0.0.1", PEER_PORT))
        self.sock.setblocking(False)
        self.target = ("127.0.0.1", LAUNCHER_PORT)

    def present(self):
        self.sock.sendto(wire(PRESENCE_TYPE), self.target)

    def game_frames_received(self):
        """Counts the game frames the host sent since the last call; everything else is dropped."""
        count = 0
        while True:
            try:
                data, _ = self.sock.recvfrom(65536)
            except BlockingIOError:
                return count
            if len(data) >= 4 and struct.unpack_from("<H", data)[0] == GAME_TYPE:
                count += 1

    def stay(self, seconds):
        """Presence once a second for `seconds`; returns the game frames received."""
        received = 0
        deadline = time.time() + seconds
        while time.time() < deadline:
            self.present()
            time.sleep(TICK_S)
            received += self.game_frames_received()
        return received

    def silent(self, seconds):
        """Sends nothing for `seconds`; returns the game frames the host still sent."""
        received = 0
        deadline = time.time() + seconds
        while time.time() < deadline:
            time.sleep(TICK_S)
            received += self.game_frames_received()
        return received

    def flood(self, per_second, seconds):
        batches = max(1, int(1 / BATCH_S))
        frame = wire(GAME_TYPE, payload=b"flood")
        sent = 0
        deadline = time.time() + seconds
        while time.time() < deadline:
            self.present()
            for _ in range(batches):
                for _ in range(per_second // batches):
                    self.sock.sendto(frame, self.target)
                    sent += 1
                time.sleep(BATCH_S)
            self.game_frames_received()
        return sent


def start_session(work):
    source, game = work / "source", work / "game"
    for d in (source, game):
        d.mkdir(parents=True)
    (source / "data0.bin").write_bytes(os.urandom(1000))
    host_log = work / "host.log"
    host = launcher(["host", "re0", "--transport", "local", "--no-launch", "--local-port", str(LAUNCHER_PORT),
                     "--peer-port", str(PEER_PORT), "--bridge-port", str(BRIDGE_PORT), "--save-source", str(source),
                     "--game-dir", str(game)], host_log, EXE)
    if not wait_for(lambda: "Loopback bridge listening" in host_log.read_text(), TRANSFER_TIMEOUT_S):
        raise RuntimeError("the host launcher did not start")
    adapter_log = work / "adapter.log"
    adapter = subprocess.Popen([sys.executable, "-u", str(Path(__file__).with_name("fake_adapter.py")), "--port", str(BRIDGE_PORT)],
                               stdout=open(adapter_log, "w"), stderr=subprocess.STDOUT)
    return host, adapter, host_log, adapter_log


def stop(host, adapter):
    adapter.kill()
    host.send_signal(signal.CTRL_BREAK_EVENT)
    try:
        host.wait(EXIT_TIMEOUT_S)
    except subprocess.TimeoutExpired:
        host.kill()


def check_gap(seconds, guest, adapter_log):
    failures = []
    guest.stay(WARMUP_S)
    during = guest.silent(seconds)
    guest.stay(RETURN_S)
    text = adapter_log.read_text()
    ups, downs = len(PEER_UP.findall(text)), text.count("PEER_DOWN")
    if seconds + SLACK_S < PRESENCE_TIMEOUT_S + GRACE_S:
        if downs:
            failures.append(f"PEER_DOWN after a {seconds} s gap (the hold is {GRACE_S} s)")
        if ups != 2:
            failures.append(f"expected a second PEER_UP for slot 1, saw {ups} PEER_UP")
        if not during:
            failures.append("no game frames reached the peer during the hold")
    elif seconds - SLACK_S > PRESENCE_TIMEOUT_S + GRACE_S:
        if not downs:
            failures.append(f"no PEER_DOWN after a {seconds} s gap")
    else:
        failures.append(f"a {seconds} s gap is too close to the {PRESENCE_TIMEOUT_S + GRACE_S} s hold to judge")
    return failures


def check_flood(per_second, guest, adapter, host_log, adapter_log):
    failures = []
    guest.stay(WARMUP_S)
    guest.flood(per_second, FLOOD_S)
    time.sleep(BUDGET_REPORT_S + SLACK_S)
    guest.present()
    log = host_log.read_text()
    if "Peer budget:" not in log:
        failures.append("no Peer budget line in the launcher log")
    if "PEER_DOWN" in adapter_log.read_text():
        failures.append("the flood ended the session (PEER_DOWN)")
    if "link down" in log:
        failures.append("the adapter link went down")
    if adapter.poll() is not None:
        failures.append("the adapter lost its link")
    return failures


def main():
    parser = argparse.ArgumentParser()
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--gap", type=float)
    group.add_argument("--flood", type=int)
    args = parser.parse_args()

    work = Path(tempfile.mkdtemp(prefix="cf_peer_link_"))
    host, adapter, host_log, adapter_log = start_session(work)
    guest = FakeGuest()
    try:
        if args.gap is not None:
            failures = check_gap(args.gap, guest, adapter_log)
        else:
            failures = check_flood(args.flood, guest, adapter, host_log, adapter_log)
    finally:
        stop(host, adapter)
    print("FAIL: " + "; ".join(failures) if failures else "peer_link_test OK")
    print(f"logs in {work}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
