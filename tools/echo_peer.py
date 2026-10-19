"""Echo peer for one-PC tests: connects to a launcher as a fake adapter and sends every received
PLAYER_STATE (0x0100), PAD_FRAME (0x0101), POSE_STATE (0x0140), MOTION_EVENT (0x0141) and WEAPON_FX (0x0142) game frame
back to its sender after a delay. --offset-x shifts the echoed PLAYER_STATE and POSE_STATE position, and the positions
inside WEAPON_FX entries, so the ghost and its effects appear beside the sender instead of on top of it.

Usage: python echo_peer.py --port 27961 [--game re0] [--delay 1.0] [--offset-x 0]
"""
import argparse
import heapq
import socket
import struct
import threading
import time

PROTO = 1
HELLO = 0x0001
HEARTBEAT = 0x0020
PLAYER_STATE = 0x0100
POSE_STATE = 0x0140
WEAPON_FX = 0x0142
ECHOED_TYPES = (PLAYER_STATE, 0x0101, POSE_STATE, 0x0141, WEAPON_FX)
# Byte offset of pos[0] in the payload: PLAYER_STATE is u32 seq, f32 pos[3], ...; POSE_STATE v2 is u32 sender ms, u32 seq, f32 pos[3], ...
POS_X_OFFSETS = {PLAYER_STATE: 4, POSE_STATE: 8}
# WEAPON_FX: a 12 byte header (u32 sender ms, u32 seq, u8 character, u8 count, u16 pad), then count entries that start with
# their op. op -> (entry size, offset of the first position float or None). Op 3 (shoot) is 4 bytes for a casing or
# magazine and 44 for a projectile (kind byte at +2 is 0).
WEAPON_FX_HEADER = 12
WEAPON_FX_COUNT_OFFSET = 9
WEAPON_FX_SHOOT = 3
WEAPON_FX_PROJECTILE_SIZE = 44
WEAPON_FX_ENTRIES = {1: (8, None), 2: (8, None), 4: (32, 8), 5: (16, 4), 6: (4, None), 7: (4, None)}
WEAPON_FX_SHOOT_POS = 4
FLAG_RELIABLE = 1
HEARTBEAT_S = 1.0
IDLE_POLL_S = 0.005


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


def shift_weapon_fx(payload, offset_x):
    """Adds offset_x to the x of every position inside a WEAPON_FX payload (in place); stops at an unknown entry."""
    at = WEAPON_FX_HEADER
    for _ in range(payload[WEAPON_FX_COUNT_OFFSET]):
        op = payload[at]
        if op == WEAPON_FX_SHOOT:
            projectile = payload[at + 2] == 0
            size, pos = (WEAPON_FX_PROJECTILE_SIZE, WEAPON_FX_SHOOT_POS) if projectile else (4, None)
        elif op in WEAPON_FX_ENTRIES:
            size, pos = WEAPON_FX_ENTRIES[op]
        else:
            return
        if pos is not None:
            (x,) = struct.unpack_from("<f", payload, at + pos)
            struct.pack_into("<f", payload, at + pos, x + offset_x)
        at += size


class Echo:
    def __init__(self, sock, delay, offset_x):
        self.sock = sock
        self.delay = delay
        self.offset_x = offset_x
        self.pending = []  # heap of (due, order, frame bytes)
        self.order = 0
        self.lock = threading.Lock()
        self.send_lock = threading.Lock()
        self.echoed = 0

    def send(self, data):
        with self.send_lock:
            self.sock.sendall(data)

    def receive_loop(self):
        while True:
            (length,) = struct.unpack("<I", read_exact(self.sock, 4))
            body = read_exact(self.sock, length)
            msg_type, flags, slot = struct.unpack("<HBB", body[:4])
            if msg_type in ECHOED_TYPES:
                payload = bytearray(body[4:])
                if msg_type in POS_X_OFFSETS and self.offset_x:
                    offset = POS_X_OFFSETS[msg_type]
                    (x,) = struct.unpack_from("<f", payload, offset)
                    struct.pack_into("<f", payload, offset, x + self.offset_x)
                if msg_type == WEAPON_FX and self.offset_x:
                    shift_weapon_fx(payload, self.offset_x)
                frame = encode(msg_type, flags, slot, bytes(payload))
                with self.lock:
                    self.order += 1
                    heapq.heappush(self.pending, (time.monotonic() + self.delay, self.order, frame))

    def send_loop(self):
        last_heartbeat = 0.0
        while True:
            now = time.monotonic()
            if now - last_heartbeat >= HEARTBEAT_S:
                last_heartbeat = now
                self.send(encode(HEARTBEAT, 0, 0))
            while True:
                with self.lock:
                    if not self.pending or self.pending[0][0] > now:
                        break
                    _, _, frame = heapq.heappop(self.pending)
                self.send(frame)
                self.echoed += 1
            time.sleep(IDLE_POLL_S)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=27960)
    parser.add_argument("--game", default="re0")
    parser.add_argument("--delay", type=float, default=1.0)
    parser.add_argument("--offset-x", type=float, default=0.0)
    args = parser.parse_args()

    sock = socket.create_connection(("127.0.0.1", args.port))
    game = args.game.encode("ascii")
    sock.sendall(encode(HELLO, FLAG_RELIABLE, 0, struct.pack("<HB", PROTO, len(game)) + game))
    echo = Echo(sock, args.delay, args.offset_x)
    threading.Thread(target=echo.receive_loop, daemon=True).start()
    print(f"echo_peer connected to 127.0.0.1:{args.port}, delay {args.delay}s", flush=True)
    echo.send_loop()


if __name__ == "__main__":
    try:
        main()
    except (ConnectionError, KeyboardInterrupt):
        pass
