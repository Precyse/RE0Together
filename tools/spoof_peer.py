"""Fake local-transport peer that stamps a wrong slot byte on a game frame, to check the launcher overwrites it.

Usage: python spoof_peer.py --peer-port 27962 --launcher-port 27961 [--slot 7]
Pair with a launcher started with --transport local --local-port <launcher-port> --peer-port <peer-port>.
"""
import argparse
import socket
import struct
import time

PRESENCE_TYPE = 0x0030
GAME_TYPE = 0x0100
FLAG_RELIABLE = 1
TICK_S = 1.0
DURATION_S = 8


def wire(msg_type, slot, payload=b""):
    return struct.pack("<HBB", msg_type, FLAG_RELIABLE, slot) + payload


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--peer-port", type=int, required=True)
    parser.add_argument("--launcher-port", type=int, required=True)
    parser.add_argument("--slot", type=int, default=7)
    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("127.0.0.1", args.peer_port))
    target = ("127.0.0.1", args.launcher_port)
    for _ in range(DURATION_S):
        sock.sendto(wire(PRESENCE_TYPE, 0), target)
        sock.sendto(wire(GAME_TYPE, args.slot, f"spoofed slot byte {args.slot}".encode()), target)
        time.sleep(TICK_S)


if __name__ == "__main__":
    main()
