"""Echo peer for one-PC tests: connects to a launcher as a fake adapter and sends every received
PLAYER_STATE (0x0100) and PAD_FRAME (0x0101) game frame back to its sender after a delay. --offset-x shifts the
echoed PLAYER_STATE position so the ghost stands beside the sender instead of colliding with it.

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
ECHOED_TYPES = (PLAYER_STATE, 0x0101)
POS_X_OFFSET = 4  # PLAYER_STATE payload: u32 seq, f32 pos[3], f32 quat[4]
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
                if msg_type == PLAYER_STATE and self.offset_x:
                    (x,) = struct.unpack_from("<f", payload, POS_X_OFFSET)
                    struct.pack_into("<f", payload, POS_X_OFFSET, x + self.offset_x)
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
