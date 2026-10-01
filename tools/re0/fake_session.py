"""Stand-in launcher with a scripted guest, for testing the RE0 adapter on one PC without a second player.

The adapter connects to it as to its launcher; it is the host (slot 0) and a fake guest (slot 1) owns Billy. Commands
are read from a text file, one per line, as they are appended:
    door <scene> <entry>   the fake player's character goes through a door (DOOR_CHANGE), then reports that room
    room <scene>           the fake player reports this room (ROOM_STATE every second); "room host" follows the game
    party                  the guest asks for the other party mode (PARTY_REQUEST)
    place <scene> <x> <y> <z>  an event on the fake's side moved the game's character (CHARACTER_PLACE)
The host's door arguments are printed as they arrive, so real doors can be replayed for Billy.

With --guest the roles swap: the game is the guest (Billy) and the fake is the host (Rebecca); it announces ownership
and the party mode, and answers the game's SNAPSHOT_REQUEST with a join snapshot built from the game's own flags and
inventories plus the places given by "snapshot <billy scene> <billy entry> <billy x y z> <rebecca scene> <rebecca entry>"
(sent when the next request arrives). "party" then sets the host's mode directly.

usage: python fake_session.py [--port 27960] [--commands fake_commands.txt] [--guest]
"""
import argparse
import os
import socket
import struct
import sys
import threading
import time

PROTO_WELCOME, PROTO_PEER_UP, PROTO_HEARTBEAT = 0x0002, 0x0003, 0x0020
ROOM_STATE, PARTY_REQUEST, DOOR_CHANGE, CHARACTER_PLACE = 0x0104, 0x0109, 0x010B, 0x0113
SNAPSHOT_REQUEST, JOIN_SNAPSHOT = 0x010D, 0x010E
OWNERSHIP, PARTY_MODE = 0x0102, 0x010A
FLAG_RELIABLE = 1
HOST_SLOT, GUEST_SLOT, MAX_PLAYERS, EPOCH = 0, 1, 2, 1
FLAG_MANAGER, FLAG_BITS, FLAG_BYTES = 0xDCC014, 0x20, 0x11C
ITEMS, BILLY_ITEMS, REBECCA_ITEMS, ITEM_BLOCK = 0xDCBF44, 0x64, 0x24, 0x40
REAL_DOOR_ARGS = (33, 1, 0)  # a3, a4, flag of a plain door seen in game
GUEST_STEAM_ID = 76561190000000001
BILLY, REBECCA = 0, 1
DOOR_FLAGS = 0x20001
TICK_S = 1.0
POLL_S = 0.1


def encode(msg_type, slot, payload=b"", flags=FLAG_RELIABLE):
    return struct.pack("<IHBB", 4 + len(payload), msg_type, flags, slot) + payload


def read_exact(sock, n):
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("adapter closed the link")
        buf += chunk
    return buf


def game_bytes(address, offset, size):
    sys.argv = sys.argv[:1]
    import probe
    proc = probe.Proc(probe.find_pid())
    return proc.read(proc.u32(address) + offset, size)


def place(scene, entry, character, pos):
    a3, a4, flag = REAL_DOOR_ARGS
    door = struct.pack("<5IB3x", scene, entry, a3, a4, flag, character)
    return struct.pack("<BBH", 1, 0, scene) + door + struct.pack("<3f4f", *pos, 0, 0, 0, 1)


class Session:
    def __init__(self, sock, guest):
        self.guest = guest
        self.peer_slot = HOST_SLOT if guest else GUEST_SLOT
        self.character = REBECCA if guest else BILLY  # the fake player's own character
        self.party = 0
        self.snapshot = None
        self.sock = sock
        self.lock = threading.Lock()
        self.guest_scene = None  # None = follow the host's room
        self.host_scene = 0xFFFF

    def send(self, msg_type, payload):
        with self.lock:
            self.sock.sendall(encode(msg_type, self.peer_slot, payload))

    def room_state(self):
        scene = self.host_scene if self.guest_scene is None else self.guest_scene
        self.send(ROOM_STATE, struct.pack("<HBB", scene, 0, 0))

    def receive_loop(self):
        while True:
            (length,) = struct.unpack("<I", read_exact(self.sock, 4))
            body = read_exact(self.sock, length)
            msg_type, _, _ = struct.unpack("<HBB", body[:4])
            payload = body[4:]
            if msg_type == ROOM_STATE:
                scene, partner_in_room, claim = struct.unpack("<HBB", payload)
                if scene != self.host_scene:
                    print(f"host room: scene {scene:#04x} partner_in_room={partner_in_room} claim={claim}", flush=True)
                self.host_scene = scene
            elif msg_type == DOOR_CHANGE:
                room, entry, a3, a4, flag, character = struct.unpack("<5IB3x", payload)
                print(f"host door: room {room:#x} entry {entry} a3 {a3} a4 {a4} flag {flag:#x} character {character}",
                      flush=True)
            elif msg_type == PARTY_MODE:
                print(f"party mode: {'team' if payload[0] == 0 else 'leave behind'}", flush=True)
            elif msg_type == SNAPSHOT_REQUEST and self.snapshot:
                self.send(JOIN_SNAPSHOT, self.snapshot)
                print("join snapshot sent", flush=True)
                self.snapshot = None

    def command(self, line):
        words = line.split()
        if not words:
            return
        if words[0] == "door":
            room, entry = int(words[1], 0), int(words[2], 0)
            self.send(DOOR_CHANGE, struct.pack("<5IB3x", room, entry, 0, 0, DOOR_FLAGS, self.character))
            self.guest_scene = room
            self.room_state()
        elif words[0] == "room":
            self.guest_scene = None if words[1] == "host" else int(words[1], 0)
            self.room_state()
        elif words[0] == "party" and self.guest:
            self.party ^= 1
        elif words[0] == "party":
            self.send(PARTY_REQUEST, b"")
        elif words[0] == "snapshot":
            billy_scene, billy_entry = int(words[1], 0), int(words[2], 0)
            billy_pos = tuple(float(v) for v in words[3:6])
            rebecca_scene, rebecca_entry = int(words[6], 0), int(words[7], 0)
            self.snapshot = (place(billy_scene, billy_entry, BILLY, billy_pos) +
                             place(rebecca_scene, rebecca_entry, REBECCA, (0, 0, 0)) +
                             game_bytes(ITEMS, BILLY_ITEMS, ITEM_BLOCK) + game_bytes(ITEMS, REBECCA_ITEMS, ITEM_BLOCK) +
                             game_bytes(FLAG_MANAGER, FLAG_BITS, FLAG_BYTES))
        elif words[0] == "place":
            scene = int(words[1], 0)
            x, y, z = (float(v) for v in words[2:5])
            self.send(CHARACTER_PLACE, struct.pack("<BBH3f4f", BILLY + REBECCA - self.character, 0, scene, x, y, z, 0, 0, 0, 1))
        print(f"> {line}", flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=27960)
    parser.add_argument("--commands", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "fake_commands.txt"))
    parser.add_argument("--guest", action="store_true", help="the game is the guest, the fake the host")
    args = parser.parse_args()
    local_slot, peer_slot = (GUEST_SLOT, HOST_SLOT) if args.guest else (HOST_SLOT, GUEST_SLOT)
    open(args.commands, "w").close()

    server = socket.socket()
    server.bind(("127.0.0.1", args.port))
    server.listen(1)
    print(f"waiting for the adapter on 127.0.0.1:{args.port}, commands in {args.commands}", flush=True)
    sock, _ = server.accept()
    (length,) = struct.unpack("<I", read_exact(sock, 4))
    read_exact(sock, length)  # HELLO
    sock.sendall(encode(PROTO_WELCOME, HOST_SLOT, struct.pack("<BBBI", local_slot, HOST_SLOT, MAX_PLAYERS, EPOCH)))
    name = b"fake peer"
    sock.sendall(encode(PROTO_PEER_UP, HOST_SLOT, struct.pack("<BQB", peer_slot, GUEST_STEAM_ID, len(name)) + name))
    session = Session(sock, args.guest)
    threading.Thread(target=session.receive_loop, daemon=True).start()
    print(f"adapter linked as the {'guest (Billy)' if args.guest else 'host (Rebecca)'}", flush=True)

    done = 0
    last_tick = 0.0
    while True:
        now = time.monotonic()
        if now - last_tick >= TICK_S:
            last_tick = now
            with session.lock:
                sock.sendall(encode(PROTO_HEARTBEAT, HOST_SLOT, flags=0))
            session.room_state()
            if args.guest:
                session.send(OWNERSHIP, bytes((GUEST_SLOT, HOST_SLOT)))
                session.send(PARTY_MODE, bytes((session.party,)))
        lines = open(args.commands).read().splitlines()
        for line in lines[done:]:
            session.command(line)
        done = len(lines)
        time.sleep(POLL_S)


if __name__ == "__main__":
    main()
