"""Fake game adapter for testing the launcher: HELLO, print control messages, send a game frame every second.

Usage: python fake_adapter.py --port 27960 [--game re0] [--name A]
"""
import argparse
import socket
import struct
import threading
import time

PROTO = 1
GAME_TYPE = 0x0100
SLOT_ALL = 0xFF
FLAG_RELIABLE = 1
TICK_S = 1.0

CONTROL_NAMES = {0x0002: "WELCOME", 0x0003: "PEER_UP", 0x0004: "PEER_DOWN", 0x0005: "REJECT",
                 0x0012: "PEER_STATS", 0x0020: "HEARTBEAT"}


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


def describe(msg_type, slot, payload):
    if msg_type == 0x0002:
        local, host, max_players, epoch = struct.unpack("<BBBI", payload)
        return f"local_slot={local} host_slot={host} max={max_players} epoch={epoch}"
    if msg_type == 0x0003:
        peer_slot, steam_id, name_len = struct.unpack("<BQB", payload[:10])
        return f"slot={peer_slot} id={steam_id} name={payload[10:10 + name_len].decode()}"
    if msg_type == 0x0004:
        return f"slot={payload[0]}"
    if msg_type == 0x0005:
        return f"reason={payload[1:1 + payload[0]].decode()}"
    if msg_type == 0x0012:
        peer_slot, rtt = struct.unpack("<BH", payload)
        return f"slot={peer_slot} rtt_ms={rtt}"
    return ""


def receive_loop(sock, name):
    while True:
        (length,) = struct.unpack("<I", read_exact(sock, 4))
        body = read_exact(sock, length)
        msg_type, flags, slot = struct.unpack("<HBB", body[:4])
        payload = body[4:]
        if msg_type == 0x0020:
            continue
        if msg_type >= GAME_TYPE:
            print(f"[{name}] GAME type=0x{msg_type:04x} from_slot={slot} payload={payload.decode(errors='replace')}", flush=True)
        else:
            label = CONTROL_NAMES.get(msg_type, f"0x{msg_type:04x}")
            print(f"[{name}] {label} {describe(msg_type, slot, payload)}", flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=27960)
    parser.add_argument("--game", default="re0")
    parser.add_argument("--name", default="adapter")
    args = parser.parse_args()

    sock = socket.create_connection(("127.0.0.1", args.port))
    game = args.game.encode("ascii")
    sock.sendall(encode(0x0001, FLAG_RELIABLE, 0, struct.pack("<HB", PROTO, len(game)) + game))
    threading.Thread(target=receive_loop, args=(sock, args.name), daemon=True).start()

    n = 0
    while True:
        sock.sendall(encode(0x0020, 0, 0))
        n += 1
        text = f"{args.name} frame {n}".encode()
        sock.sendall(encode(GAME_TYPE, FLAG_RELIABLE, SLOT_ALL, text))
        time.sleep(TICK_S)


if __name__ == "__main__":
    try:
        main()
    except (ConnectionError, KeyboardInterrupt):
        pass
