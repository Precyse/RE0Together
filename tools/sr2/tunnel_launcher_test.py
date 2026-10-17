"""The launcher side of the SR2 steam tunnel on one PC: two launchers on local transport (Saints Row 2 profile,
bridged because the host's `transport` option is steam) and two stand-in adapters. Checks that WELCOME carries the local
id (the local transport's id is its UDP port), that GAME_DATAGRAMs go unicast and broadcast both ways with the sender's
slot, that a guest whose host chose another transport gets no adapter link, and that a guest which leaves and comes back
keeps its slot and its id, so datagrams follow it. LocalTransport has one peer, so a third member cannot join here.
Usage: python tools/sr2/tunnel_launcher_test.py   (the launcher must be built in Release; COOP_LAUNCHER overrides the exe)"""
import signal
import socket
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import wire
from save_sync_test import CONNECT_RETRY_S, EXIT_TIMEOUT_S, launcher, wait_for

PORTS = (27971, 27972)
BRIDGE_PORTS = (27973, 27974)
GAME = "sr2"
HOST_SLOT = 0
GUEST_SLOT = 1
LINK_TIMEOUT_S = 20
NO_LINK_WAIT_S = 4
UNICAST_DST = 0x0A010203
PING = bytes(range(wire.PING_BYTES))
PONG = b"pong" * 25


class Adapter:
    """A stand-in game adapter: connects to a launcher's bridge, says HELLO and collects every frame it gets."""

    def __init__(self, port):
        deadline = time.time() + LINK_TIMEOUT_S
        while True:
            try:
                self.sock = socket.create_connection(("127.0.0.1", port))
                break
            except OSError:
                if time.time() > deadline:
                    raise
                time.sleep(CONNECT_RETRY_S)
        self.frames = []
        self.lock = threading.Lock()
        self.sock.sendall(wire.hello(GAME))
        threading.Thread(target=self.collect, daemon=True).start()

    def collect(self):
        try:
            while True:
                frame = wire.read_frame(self.sock)
                if frame[0] != wire.MSG_HEARTBEAT:
                    with self.lock:
                        self.frames.append(frame)
        except (ConnectionError, OSError):
            pass

    def find(self, msg_type, match=lambda slot, payload: True):
        with self.lock:
            return next((f for f in self.frames if f[0] == msg_type and match(f[2], f[3])), None)

    def wait(self, msg_type, match=lambda slot, payload: True):
        wait_for(lambda: self.find(msg_type, match), LINK_TIMEOUT_S)
        return self.find(msg_type, match)

    def send_datagram(self, to_slot, dst, data):
        self.sock.sendall(wire.encode(wire.MSG_GAME_DATAGRAM, 0, to_slot, wire.datagram(dst, data)))

    def close(self):
        self.sock.close()


def start(role, game_dir, host_options=None):
    index = 0 if role == "host" else 1
    command = ["host", GAME] if role == "host" else ["join", "0", "--game", GAME]
    args = [*command, "--transport", "local", "--no-launch", "--local-port", str(PORTS[index]),
            "--peer-port", str(PORTS[1 - index]), "--bridge-port", str(BRIDGE_PORTS[index]), "--game-dir", str(game_dir)]
    if host_options:
        args += ["--host-options", host_options]
    return launcher(args, game_dir.parent / f"{role}.log")


def stop(proc, failures):
    proc.send_signal(signal.CTRL_BREAK_EVENT)
    try:
        proc.wait(EXIT_TIMEOUT_S)
    except subprocess.TimeoutExpired:
        proc.kill()
        failures.append("a launcher did not exit on Ctrl+Break")


def delivered(adapter, from_slot, data):
    return adapter.wait(wire.MSG_GAME_DATAGRAM, lambda slot, payload: slot == from_slot and payload[wire.DST.size:] == data)


def check_welcome(adapter, local_slot, local_id, peer_id, failures, who):
    welcome = adapter.wait(wire.MSG_WELCOME)
    if not welcome or len(welcome[3]) != wire.WELCOME.size:
        failures.append(f"{who} WELCOME missing or not {wire.WELCOME.size} bytes")
        return
    slot, host, _max_players, _epoch, steam_id = wire.WELCOME.unpack(welcome[3])
    if (slot, host, steam_id) != (local_slot, HOST_SLOT, local_id):
        failures.append(f"{who} WELCOME slot/host/id {slot}/{host}/{steam_id}")
    peer = adapter.wait(wire.MSG_PEER_UP)
    if not peer or wire.PEER_UP_FIXED.unpack(peer[3][:wire.PEER_UP_FIXED.size])[:2] != (1 - local_slot, peer_id):
        failures.append(f"{who} PEER_UP is not the other player")


def check_datagrams(host, guest, failures):
    host.send_datagram(GUEST_SLOT, UNICAST_DST, PING)
    if not delivered(guest, HOST_SLOT, PING):
        failures.append("host to guest unicast not delivered with source slot 0")
    guest.send_datagram(HOST_SLOT, UNICAST_DST, PONG)
    if not delivered(host, GUEST_SLOT, PONG):
        failures.append("guest to host unicast not delivered with source slot 1")
    broadcast = b"broadcast from the host"
    host.send_datagram(wire.SLOT_ALL, wire.BROADCAST, broadcast)
    if not delivered(guest, HOST_SLOT, broadcast):
        failures.append("host broadcast not delivered")
    broadcast = b"broadcast from the guest"
    guest.send_datagram(wire.SLOT_ALL, wire.BROADCAST, broadcast)
    if not delivered(host, GUEST_SLOT, broadcast):
        failures.append("guest broadcast not delivered")
    dst, _data = wire.split_datagram(guest.find(wire.MSG_GAME_DATAGRAM, lambda slot, payload: payload.endswith(PING))[3])
    if dst != UNICAST_DST:
        failures.append("the destination address was changed on the way")


def check_rejoin(host, work, guest_proc, failures):
    """The guest launcher leaves and comes back: same slot, same id, and datagrams reach its new adapter. Returns the
    new guest launcher."""
    stop(guest_proc, failures)
    if not host.wait(wire.MSG_PEER_DOWN):
        failures.append("host never saw PEER_DOWN when the guest left")
    guest_proc = start("guest", work / "guest_game", "transport=steam")
    guest = Adapter(BRIDGE_PORTS[1])
    welcome = guest.wait(wire.MSG_WELCOME)
    if not welcome or wire.WELCOME.unpack(welcome[3])[0] != GUEST_SLOT:
        failures.append("the returning guest did not get slot 1 again")
    again = b"after the rejoin"
    host.send_datagram(GUEST_SLOT, UNICAST_DST, again)
    if not delivered(guest, HOST_SLOT, again):
        failures.append("datagram did not follow the returning guest")
    guest.close()
    return guest_proc


def check_unbridged(work, guest_proc, failures):
    """A guest whose host chose the direct transport has no adapter link. Returns the new guest launcher."""
    stop(guest_proc, failures)
    guest_proc = start("guest", work / "guest_game", "transport=native")
    time.sleep(NO_LINK_WAIT_S)
    try:
        socket.create_connection(("127.0.0.1", BRIDGE_PORTS[1]), timeout=1).close()
        failures.append("a native-transport guest still opened the adapter link")
    except OSError:
        pass
    return guest_proc


def main():
    work = Path(tempfile.mkdtemp(prefix="cf_tunnel_"))
    host_game, guest_game = work / "host_game", work / "guest_game"
    host_game.mkdir()
    guest_game.mkdir()
    failures = []
    guest_proc = None
    host_proc = start("host", host_game)
    guest_proc = start("guest", guest_game, "transport=steam")
    try:
        host, guest = Adapter(BRIDGE_PORTS[0]), Adapter(BRIDGE_PORTS[1])
        check_welcome(host, HOST_SLOT, PORTS[0], PORTS[1], failures, "host")
        check_welcome(guest, GUEST_SLOT, PORTS[1], PORTS[0], failures, "guest")
        check_datagrams(host, guest, failures)
        guest.close()
        guest_proc = check_rejoin(host, work, guest_proc, failures)
        guest_proc = check_unbridged(work, guest_proc, failures)
        host.close()
    finally:
        for proc in (guest_proc, host_proc):
            if proc is not None and proc.poll() is None:
                stop(proc, failures)
    print("FAIL: " + "; ".join(failures) if failures else "tunnel_launcher_test OK")
    print(f"logs in {work}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
