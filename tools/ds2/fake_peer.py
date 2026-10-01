"""Scripted DS2 peer for one-PC tests: connects to a launcher's adapter port as the second player and walks a circle
around the local player, sending PLAYER_STATE (0x0100) at 60 Hz, so the remote body can be watched walking while the
local Sam stands still.

usage: python fake_peer.py --port 27990 [--radius 3] [--speed 1.4] [--offset-x 0]
The circle is centred on the first PLAYER_STATE received from the local player (plus --offset-x along world X).
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


def drain(sock):
    """Keep reading so the launcher's buffers never fill."""
    try:
        while True:
            (length,) = struct.unpack("<I", read_exact(sock, 4))
            read_exact(sock, length)
    except (ConnectionError, OSError):
        pass


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=27990)
    parser.add_argument("--radius", type=float, default=3.0)
    parser.add_argument("--speed", type=float, default=1.4)
    parser.add_argument("--offset-x", type=float, default=0.0)
    args = parser.parse_args()

    sock = socket.create_connection(("127.0.0.1", args.port))
    sock.sendall(encode(HELLO, FLAG_RELIABLE, 0, struct.pack("<HB", PROTO, 3) + b"ds2"))
    cx, cy, cz = wait_for_centre(sock)
    cx += args.offset_x
    threading.Thread(target=drain, args=(sock,), daemon=True).start()
    print(f"fake_peer: circling ({cx:.1f}, {cy:.1f}, {cz:.1f}) r={args.radius} at {args.speed} m/s", flush=True)

    angular = args.speed / args.radius
    start = time.monotonic()
    last_heartbeat = 0.0
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
        time.sleep(1 / SEND_HZ)


if __name__ == "__main__":
    try:
        main()
    except (ConnectionError, KeyboardInterrupt):
        pass
