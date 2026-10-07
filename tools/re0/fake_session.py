"""Stand-in launcher with a scripted guest, for testing the RE0 adapter on one PC without a second player.

The adapter connects to it as to its launcher; it is the host (slot 0) and a fake guest (slot 1) owns Billy. Commands
are read from a text file, one per line, as they are appended:
    door <scene> <entry>   the fake player's character goes through a door (DOOR_CHANGE), reports where the door leads
                           at once (a door both machines play) and that room when its door would have finished (8 s),
                           like a real peer; a game whose door is ready first waits at its door's end (room_gate)
    room <scene>           the fake player reports this room (ROOM_STATE every second); "room host" follows the game
    party                  the guest asks for the other party mode (PARTY_REQUEST)
    place <scene> <x> <y> <z>  an event on the fake's side moved the game's character (CHARACTER_PLACE)
    hp <n>                 the fake reports PLAYER_STATE with this hp every second (condition and room on the game's
                           partner status line)
    menu <0|1> [phase]     the fake's menu is open (resent every second) or closed; phase is the room phase number
                           (5 SubScreen, 8 Option, 2 Message, 10 EventDemo); the cap is 20 s except for 2, 3, 10, 12
    phase <n>              (--guest) the host announces its save slot and room phase (SAVE_SLOT) every second:
                           1 Main, 14 Dead (game over: the guest presses Continue by itself)
    floor <room> <item> <count> <x> <y> <z>  (--guest) adds a floor item to the floor snapshot sent with the next join
                           snapshot (FLOOR_SNAPSHOT)
    leave                  the fake player goes away (PEER_DOWN) and stops sending; as the host: "Host left"
    resync                 (--guest) the host asks the guest for a resync (RESYNC_REQUEST)
The host's door arguments are printed as they arrive, so real doors can be replayed for Billy, and so are the game's
room script events (EVENT_START, and EVENT_STEPS per thread: the pcs of every op the game finished), to check what a
peer would follow.

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
PLAYER_STATE, OWNERSHIP, PARTY_MODE = 0x0100, 0x0102, 0x010A
MENU_STATE, SAVE_SLOT, RESYNC_REQUEST, FLOOR_SNAPSHOT = 0x0105, 0x010F, 0x0114, 0x0115
PROTO_PEER_DOWN = 0x0004
EVENT_START, EVENT_STEPS = 0x0130, 0x0131
EVENT_START_FORMAT = "<IHHHHBBBB"  # key, scene, serial, trigger index, pc, type, subject character, kind, flags
EVENT_STEP_FORMAT = "<HHHBx"  # serial, from pc, to pc, result, flags
STEP_RESULTS = ("yield", "next", "end", "killed")
SESSION_SLOT = 0
IDENTITY_QUAT = (0.0, 0.0, 0.0, 1.0)
FLAG_RELIABLE = 1
HOST_SLOT, GUEST_SLOT, MAX_PLAYERS, EPOCH = 0, 1, 2, 1
FLAG_MANAGER, FLAG_BITS, FLAG_BYTES = 0xDCC014, 0x20, 0x11C
ITEMS, BILLY_ITEMS, REBECCA_ITEMS, ITEM_BLOCK = 0xDCBF44, 0x64, 0x24, 0x40
REAL_DOOR_ARGS = (33, 1, 0)  # a3, a4, flag of a plain door seen in game
GUEST_STEAM_ID = 76561190000000001
BILLY, REBECCA = 0, 1
TICK_S = 1.0
DOOR_SECONDS = 8.0  # a real peer reports its new room only when its door animation ends
POLL_S = 0.1
NO_SCENE = 0xFFFF
ROOM_STATE_FORMAT = "<HBBHBx"  # scene, partner in room, enemy claim, door target, door flags
DOOR_SHARED, DOOR_READY = 1, 2
DOOR_ALONE = 1  # DOOR_CHANGE flags: a room script left the partner behind


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
        self.host_scene = NO_SCENE
        self.door_target = NO_SCENE  # where the fake's running door leads
        self.door_flags = 0
        self.hp = None  # the fake's reported hp, None = no PLAYER_STATE
        self.menu = None  # (open, phase) of the fake's MENU_STATE, None = never sent
        self.phase = None  # the host's announced room phase (--guest), None = no SAVE_SLOT
        self.floor = []  # floor snapshot entries for the next join snapshot
        self.seq = 0
        self.left = False

    def send(self, msg_type, payload):
        with self.lock:
            self.sock.sendall(encode(msg_type, self.peer_slot, payload))

    def periodic(self):
        """What a real peer repeats every second."""
        if self.left:
            return
        self.room_state()
        if self.guest:
            self.send(OWNERSHIP, bytes((GUEST_SLOT, HOST_SLOT)))
            self.send(PARTY_MODE, bytes((self.party,)))
        if self.phase is not None:
            self.send(SAVE_SLOT, struct.pack("<ii", SESSION_SLOT, self.phase))
        if self.hp is not None:
            self.seq += 1
            scene = self.host_scene if self.guest_scene is None else self.guest_scene
            self.send(PLAYER_STATE, struct.pack("<I3f4fBBHiB3x", self.seq, 0, 0, 0, *IDENTITY_QUAT, self.character,
                                                int(self.guest), scene, self.hp, 0xFF))
        if self.menu is not None and self.menu[0]:
            self.send(MENU_STATE, bytes(self.menu))

    def floor_snapshot(self):
        return struct.pack("<HH", len(self.floor), 0) + b"".join(
            struct.pack("<HBBII3f3f", room, 0, 0, item, count, *pos, 0, 0, 0) for room, item, count, pos in self.floor)

    def room_state(self):
        scene = self.host_scene if self.guest_scene is None else self.guest_scene
        self.send(ROOM_STATE, struct.pack(ROOM_STATE_FORMAT, scene, 0, 0, self.door_target, self.door_flags))

    def receive_loop(self):
        while True:
            (length,) = struct.unpack("<I", read_exact(self.sock, 4))
            body = read_exact(self.sock, length)
            msg_type, _, _ = struct.unpack("<HBB", body[:4])
            payload = body[4:]
            if msg_type == ROOM_STATE:
                scene, partner_in_room, claim, door_target, door_flags = struct.unpack(ROOM_STATE_FORMAT, payload)
                if scene != self.host_scene or door_target != NO_SCENE:
                    print(f"host room: scene {scene:#04x} partner_in_room={partner_in_room} claim={claim} "
                          f"door_target={door_target:#04x} door_flags={door_flags}", flush=True)
                self.host_scene = scene
            elif msg_type == DOOR_CHANGE:
                room, entry, a3, a4, flag, character, door_flags = struct.unpack("<5IBB2x", payload)
                print(f"host door: room {room:#x} entry {entry} a3 {a3} a4 {a4} flag {flag:#x} character {character}"
                      f"{' (partner left behind)' if door_flags & DOOR_ALONE else ''}", flush=True)
            elif msg_type == EVENT_START:
                key, scene, serial, index, pc, kind_type, character, kind, flags = struct.unpack(EVENT_START_FORMAT,
                                                                                                 payload)
                print(f"event start: serial {serial} key {key:#04x} trigger {index} type {kind_type:#04x} "
                      f"{'shared' if kind else 'local'} pc {pc:#x} scene {scene:#04x} subject {character} "
                      f"flags {flags}", flush=True)
            elif msg_type == EVENT_STEPS:
                (count,) = struct.unpack_from("<H", payload)
                steps = [struct.unpack_from(EVENT_STEP_FORMAT, payload, 4 + i * 8) for i in range(count)]
                print("event steps: " + ", ".join(f"{serial}:{start:#x}->{end:#x} {STEP_RESULTS[result]}"
                                                  for serial, start, end, result in steps), flush=True)
            elif msg_type == PARTY_MODE:
                print(f"party mode: {'team' if payload[0] == 0 else 'leave behind'}", flush=True)
            elif msg_type == SNAPSHOT_REQUEST:
                print("snapshot requested", flush=True)
                if self.snapshot:
                    self.send(JOIN_SNAPSHOT, self.snapshot)
                    if self.floor:
                        self.send(FLOOR_SNAPSHOT, self.floor_snapshot())
                    print("join snapshot sent", flush=True)

    def arrive(self, scene):
        self.door_flags = DOOR_SHARED | DOOR_READY  # its door is ready to finish, then it finishes
        self.room_state()
        self.guest_scene = scene
        self.door_target = NO_SCENE
        self.door_flags = 0
        self.room_state()

    def command(self, line):
        words = line.split()
        if not words:
            return
        if words[0] == "door":
            room, entry = int(words[1], 0), int(words[2], 0)
            a3, a4, flag = REAL_DOOR_ARGS
            self.send(DOOR_CHANGE, struct.pack("<5IB3x", room, entry, a3, a4, flag, self.character))
            self.door_target = room
            self.door_flags = DOOR_SHARED
            self.room_state()
            threading.Timer(DOOR_SECONDS, self.arrive, (room,)).start()
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
        elif words[0] == "hp":
            self.hp = int(words[1])
        elif words[0] == "menu":
            self.menu = (int(words[1]), int(words[2]) if len(words) > 2 else 5)
            if not self.menu[0]:
                self.send(MENU_STATE, bytes((0, 0)))
        elif words[0] == "phase":
            self.phase = int(words[1])
        elif words[0] == "floor":
            self.floor.append((int(words[1], 0), int(words[2], 0), int(words[3]), tuple(float(v) for v in words[4:7])))
        elif words[0] == "leave":
            self.left = True
            self.send(PROTO_PEER_DOWN, bytes((self.peer_slot,)))
        elif words[0] == "resync":
            self.send(RESYNC_REQUEST, b"")
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
            session.periodic()
        lines = open(args.commands).read().splitlines()
        for line in lines[done:]:
            session.command(line)
        done = len(lines)
        time.sleep(POLL_S)


if __name__ == "__main__":
    main()
