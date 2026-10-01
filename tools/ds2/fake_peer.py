"""Scripted DS2 peer for one-PC tests: connects to a launcher's adapter port as the second player and walks a circle
around the local player, sending PLAYER_STATE (0x0100) at 60 Hz, so the remote body can be watched walking while the
local Sam stands still.

usage: python fake_peer.py --port 27990 [--radius 3] [--speed 1.4] [--offset-x 0] [--cargo TYPE:NAME,...]
                           [--host-give TYPE]
The circle is centred on the first PLAYER_STATE received from the local player (plus --offset-x along world X).
With --cargo it also acts as a guest's rack for the host's give/take menu (adapters/ds2/src/cargo_transfer.h):
reports the pieces in CARGO_LIST, gives one up with CARGO_ADD when the host sends CARGO_TAKE, and adds the kinds
the host gives. With --host-give TYPE it plays the host instead (connect it to the host launcher's bridge and the
game to the guest's): it takes the guest's first reported piece and gives one piece of TYPE.
"""
import argparse
import math
import socket
import struct
import threading
import time

PROTO = 1
HELLO = 0x0001
HEARTBEAT = 0x0020
PLAYER_STATE = 0x0100
SLOT_ALL = 0xFF
FLAG_RELIABLE = 1
SEND_HZ = 60
HEARTBEAT_S = 1.0
STATE = struct.Struct("<I3ffI")  # seq, pos[3], yaw, reserved (adapters/ds2/src/player_sync.h)
CARGO_LIST, CARGO_TAKE, CARGO_ADD = 0x0101, 0x0102, 0x0103
CARGO_ENTRY = struct.Struct("<QI44s")  # handle, type, name (adapters/ds2/src/cargo_transfer.h)
CARGO_REPORT_S = 5.0
FIRST_FAKE_HANDLE = 0xF000


class Outbox:
    """Frames for the main loop to send, so only one thread writes the socket."""

    def __init__(self):
        self.lock = threading.Lock()
        self.frames = []

    def put(self, frame):
        with self.lock:
            self.frames.append(frame)

    def take(self):
        with self.lock:
            frames, self.frames = self.frames, []
            return frames


class Rack:
    """The fake guest's cargo (handle -> (type, name)): reported to the host, given up on CARGO_TAKE."""

    def __init__(self, spec, outbox):
        self.lock = threading.Lock()
        self.outbox = outbox
        self.pieces = {}
        self.names = {}
        self.next_handle = FIRST_FAKE_HANDLE
        self.changed = True
        for item in filter(None, spec.split(",")):
            type_id, name = item.split(":", 1)
            self.names[int(type_id)] = name
            self.add(int(type_id))

    def add(self, type_id):
        with self.lock:
            self.pieces[self.next_handle] = (type_id, self.names.get(type_id, f"Cargo {type_id}"))
            self.next_handle += 1
            self.changed = True

    def report(self):
        with self.lock:
            self.changed = False
            entries = b"".join(CARGO_ENTRY.pack(h, t, n.encode()[:44]) for h, (t, n) in self.pieces.items())
            return struct.pack("<I", len(self.pieces)) + entries

    def handle(self, msg_type, slot, payload):
        if msg_type == CARGO_TAKE and len(payload) == 8:
            with self.lock:
                piece = self.pieces.pop(struct.unpack("<Q", payload)[0], None)
                self.changed = self.changed or piece is not None
            if piece:
                self.outbox.put(encode(CARGO_ADD, FLAG_RELIABLE, slot, struct.pack("<I", piece[0])))
                print(f"fake_peer: host took {piece[1]} ({piece[0]})", flush=True)
        elif msg_type == CARGO_ADD and len(payload) == 4:
            type_id = struct.unpack("<I", payload)[0]
            self.add(type_id)
            print(f"fake_peer: host gave {type_id}", flush=True)


class HostScript:
    """The fake host's side of the menu: on the guest's first CARGO_LIST it takes the first listed piece and gives
    one piece of `give_type`."""

    def __init__(self, give_type, outbox):
        self.give_type = give_type
        self.outbox = outbox
        self.done = False

    def handle(self, msg_type, slot, payload):
        if msg_type == CARGO_LIST and not self.done:
            (count,) = struct.unpack_from("<I", payload)
            pieces = [CARGO_ENTRY.unpack_from(payload, 4 + i * CARGO_ENTRY.size) for i in range(count)]
            names = ", ".join(name.rstrip(b"\0").decode() for _, _, name in pieces)
            print(f"fake_peer: guest carries {names}", flush=True)
            if pieces:
                self.outbox.put(encode(CARGO_TAKE, FLAG_RELIABLE, slot, struct.pack("<Q", pieces[0][0])))
            self.outbox.put(encode(CARGO_ADD, FLAG_RELIABLE, slot, struct.pack("<I", self.give_type)))
            self.done = True
        elif msg_type == CARGO_ADD and len(payload) == 4:
            print(f"fake_peer: guest gave {struct.unpack('<I', payload)[0]}", flush=True)


def encode(msg_type, flags, slot, payload=b""):
    return struct.pack("<IHBB", 4 + len(payload), msg_type, flags, slot) + payload


def read_exact(sock, n):
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("closed")
        buf += chunk
    return buf


def wait_for_centre(sock):
    while True:
        (length,) = struct.unpack("<I", read_exact(sock, 4))
        body = read_exact(sock, length)
        msg_type = struct.unpack_from("<H", body)[0]
        if msg_type == PLAYER_STATE and len(body) - 4 == STATE.size:
            _, x, y, z, _, _ = STATE.unpack_from(body, 4)
            return x, y, z


def drain(sock, cargo):
    """Keep reading so the launcher's buffers never fill; cargo messages go to the rack or the host script."""
    try:
        while True:
            (length,) = struct.unpack("<I", read_exact(sock, 4))
            body = read_exact(sock, length)
            msg_type, _, slot = struct.unpack_from("<HBB", body)
            if cargo:
                cargo.handle(msg_type, slot, body[4:])
    except (ConnectionError, OSError):
        pass


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=27990)
    parser.add_argument("--radius", type=float, default=3.0)
    parser.add_argument("--speed", type=float, default=1.4)
    parser.add_argument("--offset-x", type=float, default=0.0)
    parser.add_argument("--cargo", help="TYPE:NAME,... pieces the fake guest starts with")
    parser.add_argument("--host-give", type=int, help="as the host: take the guest's first piece, give one TYPE")
    args = parser.parse_args()
    outbox = Outbox()
    rack = Rack(args.cargo, outbox) if args.cargo is not None else None
    cargo = HostScript(args.host_give, outbox) if args.host_give is not None else rack

    sock = socket.create_connection(("127.0.0.1", args.port))
    sock.sendall(encode(HELLO, FLAG_RELIABLE, 0, struct.pack("<HB", PROTO, 3) + b"ds2"))
    cx, cy, cz = wait_for_centre(sock)
    cx += args.offset_x
    threading.Thread(target=drain, args=(sock, cargo), daemon=True).start()
    print(f"fake_peer: circling ({cx:.1f}, {cy:.1f}, {cz:.1f}) r={args.radius} at {args.speed} m/s", flush=True)

    angular = args.speed / args.radius
    start = time.monotonic()
    last_heartbeat = 0.0
    last_report = 0.0
    seq = 0
    while True:
        now = time.monotonic()
        a = (now - start) * angular
        x, y = cx + args.radius * math.cos(a), cy + args.radius * math.sin(a)
        heading_x, heading_y = -math.sin(a), math.cos(a)  # tangent of the circle
        yaw = math.atan2(heading_x, heading_y)
        seq += 1
        sock.sendall(encode(PLAYER_STATE, 0, SLOT_ALL, STATE.pack(seq, x, y, cz, yaw, 0)))
        if now - last_heartbeat >= HEARTBEAT_S:
            last_heartbeat = now
            sock.sendall(encode(HEARTBEAT, 0, 0))
        for frame in outbox.take():
            sock.sendall(frame)
        if rack and (rack.changed or now - last_report >= CARGO_REPORT_S):
            last_report = now
            sock.sendall(encode(CARGO_LIST, FLAG_RELIABLE, SLOT_ALL, rack.report()))
        time.sleep(1 / SEND_HZ)


if __name__ == "__main__":
    try:
        main()
    except (ConnectionError, KeyboardInterrupt):
        pass
