"""One-PC test of the SR2 steam tunnel without the game (docs/SR2_NOTES.md, milestone 10): the real tunnel code, driven by
the stand-in program tests/tunnel_standin.cpp, against tools/sr2/fake_launcher.py. Runs three exchanges, each using
the capture of the one before:
  1. guest send path   the stand-in is a guest: its ping goes to the host slot as a broadcast GAME_DATAGRAM, once a second
  2. host receive      the stand-in is a host: the recorded ping arrives from the peer's virtual address and is answered
                       with a pong broadcast to every slot
  3. guest join        the stand-in is a guest again: the recorded pong comes back, and its join request goes to the
                       host's virtual address
Usage: python tools/sr2/tunnel_adapter_test.py   (after adapters/sr2/rebuild.cmd, which builds tunnel_standin.exe)"""
import re
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

import fake_launcher
import wire

ROOT = Path(__file__).resolve().parents[2]
STANDIN = ROOT / "adapters" / "sr2" / "build" / "tunnel_standin.exe"
FAKE_LAUNCHER = Path(__file__).with_name("fake_launcher.py")
RUN_SECONDS = 5
LAUNCHER_START_S = 3.0
SETTLE_S = 2.0
HOST_SLOT = 0
SLOT_ALL = 0xFF
PING_BYTES = 20
PONG_BYTES = 120
JOIN_REQUEST_BYTES = 115
MIN_PINGS = 3
USABLE_LAST_OCTETS = 254


def virtual_address(steam_id):
    """The dotted 10.a.b.c of a SteamID64 (tunnel_rules::virtualAddress)."""
    account = steam_id & 0xFFFFFFFF
    return f"10.{(account >> 16) & 0xFF}.{(account >> 8) & 0xFF}.{1 + (account & 0xFF) % USABLE_LAST_OCTETS}"


class Process:
    """A child process whose output lines are collected."""

    def __init__(self, args):
        self.lines = []
        self.proc = subprocess.Popen([str(a) for a in args], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        threading.Thread(target=self.collect, daemon=True).start()

    def collect(self):
        for line in self.proc.stdout:
            self.lines.append(line.rstrip())

    def wait_for(self, text, seconds):
        deadline = time.time() + seconds
        while time.time() < deadline:
            if any(text in line for line in self.lines):
                return True
            time.sleep(0.05)
        return False

    def stop(self):
        if self.proc.poll() is None:
            subprocess.run(["taskkill", "/F", "/T", "/PID", str(self.proc.pid)], capture_output=True)
        self.proc.wait()


def exchange(launcher_args, role, failures, label):
    """Runs the fake launcher and the stand-in together and returns (launcher lines, stand-in lines)."""
    launcher = Process([sys.executable, FAKE_LAUNCHER, *launcher_args])
    try:
        if not launcher.wait_for("fake launcher on", LAUNCHER_START_S):
            failures.append(f"{label}: the fake launcher did not start")
            return [], []
        standin = Process([STANDIN, role, RUN_SECONDS])
        try:
            standin.proc.wait(RUN_SECONDS + SETTLE_S + LAUNCHER_START_S)
        except subprocess.TimeoutExpired:
            failures.append(f"{label}: the stand-in did not finish")
        time.sleep(SETTLE_S / 2)
        standin.stop()
        return list(launcher.lines), list(standin.lines)
    finally:
        launcher.stop()


def datagrams(lines, slot, dst, size):
    """The fake launcher's GAME_DATAGRAM lines for this destination slot, address and size."""
    pattern = f"GAME_DATAGRAM to_slot=0x{slot:02X} dst={dst} size={size} "
    return [line for line in lines if line.startswith(pattern)]


def main():
    if not STANDIN.exists():
        print(f"FAIL: {STANDIN} is missing, run adapters/sr2/rebuild.cmd")
        return 1
    failures = []
    work = Path(tempfile.mkdtemp(prefix="cf_tunnel_adapter_"))
    ping_file, pong_file = work / "ping.bin", work / "pong.bin"
    own = virtual_address(fake_launcher.DEFAULT_LOCAL_ID)
    peer = virtual_address(fake_launcher.DEFAULT_PEER_ID)

    launcher, standin = exchange(["--role", "host", "--record", ping_file], "guest", failures, "guest send path")
    pings = datagrams(launcher, HOST_SLOT, wire.dotted(wire.BROADCAST), PING_BYTES)
    if len(pings) < MIN_PINGS:
        failures.append(f"guest send path: {len(pings)} pings reached the host slot, expected {MIN_PINGS} or more")
    if f"standin: own address {own}" not in standin:
        failures.append(f"guest send path: the stand-in's own address is not {own}")
    if not ping_file.exists():
        failures.append("guest send path: no ping was recorded")

    launcher, standin = exchange(["--role", "guest", "--replay", ping_file, "--record", pong_file], "host", failures, "host receive")
    if not any(re.match(rf"standin: received {PING_BYTES} bytes, id 0, from {re.escape(peer)}:4200$", line) for line in standin):
        failures.append(f"host receive: the ping did not arrive from {peer}:4200")
    if not datagrams(launcher, SLOT_ALL, wire.dotted(wire.BROADCAST), PONG_BYTES):
        failures.append("host receive: no pong broadcast to every slot")
    if not pong_file.exists():
        failures.append("host receive: no pong was recorded")

    launcher, standin = exchange(["--role", "host", "--answer", pong_file], "guest", failures, "guest join")
    if not any(re.match(rf"standin: received {PONG_BYTES} bytes, id 1, from {re.escape(peer)}:4200$", line) for line in standin):
        failures.append(f"guest join: the pong did not arrive from {peer}:4200")
    if not datagrams(launcher, HOST_SLOT, peer, JOIN_REQUEST_BYTES):
        failures.append(f"guest join: no join request to the host's address {peer}")

    print("FAIL: " + "; ".join(failures) if failures else "tunnel_adapter_test OK")
    print(f"files in {work}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
