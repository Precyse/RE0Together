"""Stand-in DS2 launcher with a scripted second player, so the adapter gets a peer without real launchers (which reset
RE0's adapter.ini). Listens on the adapter port, answers HELLO with WELCOME + PEER_UP, then sends the peer's
PLAYER_STATE (slot 1) at 60 Hz: still for --hold seconds, then walking a circle of --radius m at --speed m/s. The
circle's centre is --offset m ahead of the first local PLAYER_STATE (--phase: the start angle, degrees; --speed 0 keeps
the peer where it starts, e.g. --offset 2 --radius 1.2 --phase 270: 2 m ahead and 1.2 m to the local player's right).
--drive ID (hex) reports the peer in that vehicle (--drive-role 0 driving, 1 riding along), parked at --drive-pos x,y,z with an upright orientation,
between the seconds --drive-window START,END after the first local state: the adapter's remote body boards the vehicle
when the reports start and leaves when they stop. --follow stands the peer beside the local player (--offset ahead, --radius to its right, same heading) wherever it goes. --guest makes the local player the guest (slot 1) of a host peer (slot 0). --echo-anim sends the local player's ANIM_STATE back as the peer's
(the remote then copies the local player through the real wire format). Prints the local player's pose.

usage: python fake_launcher.py [--port 27980] [--radius 1.2] [--speed 1.4] [--hold 5] [--offset 3.5] [--phase 0]
                               [--drive ID --drive-pos X,Y,Z --drive-window START,END]
"""
import argparse
import math
import socket
import struct
import threading
import time

HELLO, WELCOME, PEER_UP, HEARTBEAT, PLAYER_STATE, VEHICLE_STATE = 0x0001, 0x0002, 0x0003, 0x0020, 0x0100, 0x0108
ANIM_STATE = 0x010A
EQUIP_STATE = 0x010C
WEAPON_STATE, WEAPON_FIRE = 0x0124, 0x0125
WEAPON_STATE_FORMAT = struct.Struct("<HBB")  # weapon id, hand (1 = right), reserved (weapon_wire.h)
WEAPON_FIRE_FORMAT = struct.Struct("<HBBI3f3f")  # weapon id, kind, reserved, pellets, origin, direction (weapon_wire.h)
WEAPON_RESEND_SECONDS = 5.0  # the peer repeats its weapon state, so a joining adapter sees it
MUZZLE_HEIGHT_METRES = 1.0  # a shot leaves this far above the peer's feet
CARGO_LIST = 0x0101
VEHICLE_LOAD = 0x0109
VEHICLE_LOAD_HEADER = struct.Struct("<QII")  # vehicle id, count, reserved, then count u32 kinds (vehicle_load.h)
CARGO_ADD = 0x0103
CARGO_ADD_FORMAT = struct.Struct("<IB3xfIQQ")  # type, category, durability, reserved, order id, second id (cargo_transfer.h)
WORLD_ENV = 0x010D
STRUCT_CREATE, STRUCT_REMOVE = 0x010F, 0x0110
ENEMY_SPAWN, ENEMY_STATE, ENEMY_GONE, ENEMY_ANIM = 0x011B, 0x011C, 0x011D, 0x011E
ENEMY_HIT = 0x0120
ENEMY_GONE_DIED = 1  # enemy_wire.h GoneReason::Died
ENEMY_REACH_METRES = 150.0  # --enemy-hit leaves a target this far from the local player (a streamed-out enemy takes no damage)
ENEMY_HIT_FORMAT = struct.Struct("<HH16sfIiI12f")  # enemy ref {net id, reserved, uuid} + hit fields (combat_wire.h)
ENEMY_RECORD = struct.Struct("<dHI")  # seconds since the first record, message type, payload size
ENV = struct.Struct("<BBfifF64B".replace("F", "f"))  # flags, slot, hours, day, forecast clock, next threshold, 64 region types
EQUIP_ENTRY = struct.Struct("<B3xI")  # hand slot kind, cargo kind (equip_sync.h)
FLAG_RELIABLE = 1
HOST_SLOT, PEER_SLOT, MAX_PLAYERS, EPOCH = 0, 1, 2, 1
STATE = struct.Struct("<I3ffI")
VEHICLE = struct.Struct("<IIQ3f9f")  # seq, role (0 driver, 1 passenger), id, position, rotation rows
SEND_HZ = 60


def encode(msg_type, slot, payload=b"", flags=FLAG_RELIABLE):
    return struct.pack("<IHBB", 4 + len(payload), msg_type, flags, slot) + payload


def read_exact(sock, n):
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("closed")
        buf += chunk
    return buf


def load_enemy_recording(path):
    """The messages of an --enemy-record file as (seconds, type, payload)."""
    data = open(path, "rb").read()
    out, at = [], 0
    while at < len(data):
        seconds, msg_type, size = ENEMY_RECORD.unpack_from(data, at)
        at += ENEMY_RECORD.size
        out.append((seconds, msg_type, data[at:at + size]))
        at += size
    print(f"enemy: loaded {len(out)} recorded messages", flush=True)
    return out


# Every player-buildable kind with its own-field tail length (adapters/ds2/src/struct_wire.h kKinds), for --struct-all.
STRUCT_KINDS = {2: 0x00, 3: 0x00, 4: 0x08, 5: 0x18, 6: 0x00, 7: 0x00, 9: 0x08, 10: 0x18, 11: 0x08, 12: 0x08, 14: 0x28, 15: 0x08,
                16: 0x08, 20: 0x00, 21: 0x18, 22: 0x28, 23: 0x30, 26: 0x10, 32: 0x28, 33: 0x28, 34: 0x10}
SUBKIND_DEFAULT = 14  # EDSConstructionPointSubCategory Invalid: the category's own config


def kind_payload(local, kind, index):
    """STRUCT_CREATE of one kind with a zero tail, ahead of the local player on a fan (index spreads them 5 m apart)."""
    fx, fy = math.sin(local["yaw"]), math.cos(local["yaw"])
    side = (index - len(STRUCT_KINDS) / 2) * 5.0
    position = struct.pack("<3d", local["x"] + fx * 12.0 + fy * side, local["y"] + fy * 12.0 - fx * side, local["z"])
    rotation = struct.pack("<9f", fy, -fx, 0.0, fx, fy, 0.0, 0.0, 0.0, 1.0)
    fixed = struct.pack("<BBBBI16s", kind, SUBKIND_DEFAULT, 1, STRUCT_KINDS[kind], LADDER_ID + 100 + index, bytes(16)) + position + rotation + bytes(4) + struct.pack("<f", 360000.0)
    return fixed + bytes(STRUCT_KINDS[kind])


LADDER_ID = 11700  # an id the save does not use
ANCHOR_TAIL = bytes.fromhex("0000000047 7a5541".replace(" ", ""))  # the captured climbing anchor: its rope length
LADDER_AHEAD = 8.0  # metres ahead of the local player (a ladder the player grabs cannot be removed under them: the game crashes)
# The tail of the captured ladder descriptor (+0x328..+0x340), tools/ds2 out dump submit_kind10_0284
LADDER_TAIL = bytes.fromhex("0100000000" "8b6abf" "6666" "1a41" "00000000" "ffffffff" "00020000")


def ladder_payload(local, kind=10):
    """STRUCT_CREATE for a captured structure (kind 10 ladder, 11 climbing anchor), ahead of the local player and facing the way it faces."""
    fx, fy = math.sin(local["yaw"]), math.cos(local["yaw"])
    tail = LADDER_TAIL if kind == 10 else ANCHOR_TAIL
    position = struct.pack("<3d", local["x"] + fx * LADDER_AHEAD, local["y"] + fy * LADDER_AHEAD, local["z"])
    rotation = struct.pack("<9f", 0.0, 0.0, -1.0, fx, fy, 0.0, fy, -fx, 0.0)  # as the captured ladder: its long axis is the first row (straight down), then heading, then the horizontal right
    transform = position + rotation + bytes(4)
    fixed = struct.pack("<BBBBI16s", kind, 3 if kind == 10 else 14, 1, len(tail), LADDER_ID, bytes(16)) + transform + struct.pack("<f", 360000.0)
    return fixed + tail


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--port", type=int, default=27980)
    p.add_argument("--radius", type=float, default=1.2)
    p.add_argument("--speed", type=float, default=1.4)
    p.add_argument("--hold", type=float, default=5.0)
    p.add_argument("--offset", type=float, default=3.5)
    p.add_argument("--phase", type=float, default=0.0, help="start angle on the circle, degrees (90 = Sam's left when he faces +X)")
    p.add_argument("--drive", type=lambda v: int(v, 16), help="vehicle id (hex) the peer drives, parked at --drive-pos")
    p.add_argument("--drive-pos", default="0,0,0", help="x,y,z the driven vehicle is reported at")
    p.add_argument("--drive-role", type=int, default=0, help="0 = the peer drives the vehicle, 1 = it rides along")
    p.add_argument("--follow", action="store_true", help="the peer stands beside the local player wherever it goes (--offset ahead, --radius to its right)")
    p.add_argument("--guest", action="store_true", help="the local player is the guest (slot 1) and the peer is the host (slot 0)")
    p.add_argument("--echo-anim", action="store_true", help="send the local player's ANIM_STATE back as the peer's")
    p.add_argument("--env", default="", help="HOURS[,DAY[,REGION:TYPE...]]: as the host peer, send this WORLD_ENV once a second (types from a host capture)")
    p.add_argument("--struct", default="", help="ADD,REMOVE seconds after the first local state: replay the captured ladder 3 m ahead of the local player, then remove it")
    p.add_argument("--struct-kind", type=int, default=10, help="10 = the captured ladder, 11 = the captured climbing anchor")
    p.add_argument("--give-order", default="", help="SECONDS: send CARGO_ADD of the locker order piece Special Plant Seeds (order 0x1000071000018e) after that long, as the host giving it to the guest")
    p.add_argument("--host-picks-order", default="", help="SECONDS: send HOST_PICKUP of the order piece Special Plant Seeds (matched by order id at a position nowhere near it)")
    p.add_argument("--give-plain", default="", help="SECONDS: send CARGO_ADD of a Headache Pills piece with durability 123 (a worn piece)")
    p.add_argument("--drive-load", default="", help="KIND,KIND: with --drive, the cargo kinds the driven vehicle's bed holds (VEHICLE_LOAD every 5 s)")
    p.add_argument("--echo-cargo", action="store_true", help="send the local player's CARGO_LIST back as the peer's (its rack shows on the body)")
    p.add_argument("--story", default="", help="SECONDS:KIND:MISSION_ID_HEX: replay a host story event once (kind 1 start, 2 success, 3 fail)")
    p.add_argument("--enemy-record", default="", help="FILE: write the host's ENEMY_* messages the adapter sends, with their times (play as the host, near a camp)")
    p.add_argument("--enemy-replay", default="", help="FILE: send a recording made with --enemy-record as the host (use --guest)")
    p.add_argument("--enemy-id", type=int, default=0, help="the net id --enemy-hit keeps hitting while it exists (default: the nearest enemy)")
    p.add_argument("--weapon", default="", help="ID:KIND:PELLETS: the peer holds this weapon (WEAPON_STATE; kind 1 Gun, 2 ShotGun, ...), "
                    "so the body made for it has a weapon without a human drawing one")
    p.add_argument("--fire", type=float, default=0.0, help="SECONDS: with --weapon, the peer fires it this often (WEAPON_FIRE) toward the enemy --enemy-hit "
                    "targets, else straight ahead")
    p.add_argument("--enemy-hit", default="", help="SECONDS:AMOUNT: as the guest, hit the announced enemy nearest to the local player (ENEMY_HIT), again every 10 s; an enemy the host reports gone (died or despawned) is not hit again, and a death is printed")
    p.add_argument("--bt-regions", default="", help="HEXMASK: as the host (use --guest), send BT_ENV with these BT-active regions once a second")
    p.add_argument("--struct-all", type=float, default=0.0, help="SECONDS: from that many seconds after the first local state, replay one structure of EVERY buildable kind every 8 s (zero tails, default sub category)")
    p.add_argument("--enemy-delay", type=float, default=20.0, help="seconds after the first local state before --enemy-replay starts")
    p.add_argument("--echo-equip", action="store_true", help="send the local player's EQUIP_STATE back as the peer's")
    p.add_argument("--echo-weapon", action="store_true", help="send the local player's WEAPON_STATE and WEAPON_FIRE back as the peer's (weapon_sync=1: the body holds and fires Sam's weapon)")
    p.add_argument("--equip", default="", help="SLOT:KIND[,SLOT:KIND] hand pieces the peer holds (holster slot kinds 4 right arm, 5 left arm, 6 right waist, 7 left waist; kind = cargo kind id)")
    p.add_argument("--equip-window", default="0,1e9", help="START,END seconds after the first local state the peer holds them")
    p.add_argument("--drive-window", default="0,1e9", help="START,END seconds after the first local state")
    a = p.parse_args()
    server = socket.socket()
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("127.0.0.1", a.port))
    server.listen(1)
    print("waiting for the adapter", flush=True)
    while True:
        sock, _ = server.accept()
        try:
            serve(sock, a)
        except (ConnectionError, OSError) as e:
            print("link down:", e, flush=True)


def serve(sock, a):
    peer_slot = HOST_SLOT if a.guest else PEER_SLOT
    (length,) = struct.unpack("<I", read_exact(sock, 4))
    read_exact(sock, length)
    sock.sendall(encode(WELCOME, HOST_SLOT, struct.pack("<BBBI", PEER_SLOT if a.guest else HOST_SLOT, HOST_SLOT, MAX_PLAYERS, EPOCH)))
    name = b"fake peer"
    sock.sendall(encode(PEER_UP, HOST_SLOT, struct.pack("<BQB", HOST_SLOT if a.guest else PEER_SLOT, 0x1100001DEADBEEF, len(name)) + name))
    print("adapter linked", flush=True)
    local = {}
    recorded = {}
    announced = {}

    def record(body):
        now = time.monotonic()
        recorded.setdefault("first", now)
        with open(a.enemy_record, "ab") as out:
            out.write(ENEMY_RECORD.pack(now - recorded["first"], struct.unpack_from("<H", body)[0], len(body) - 4) + body[4:])

    def receive():
        while True:
            (n,) = struct.unpack("<I", read_exact(sock, 4))
            body = read_exact(sock, n)
            msg_type = struct.unpack_from("<H", body)[0]
            if msg_type == PLAYER_STATE and len(body) >= 4 + STATE.size:
                _, x, y, z, yaw, _ = STATE.unpack_from(body, 4)
                local.update(x=x, y=y, z=z, yaw=yaw)
            elif msg_type == WORLD_ENV and len(body) - 4 == ENV.size:
                flags, slot, hours, day, clock, threshold, *regions = ENV.unpack_from(body, 4)
                shown = [(i, r) for i, r in enumerate(regions) if r != 0xE]
                print(f"world env: flags {flags} slot {slot} time {hours:.3f} day {day} clock {clock:.1f} next {threshold:.1f} regions {shown}", flush=True)
            elif msg_type == ENEMY_SPAWN:
                net_id, _, uuid = struct.unpack_from("<HH16s", body, 4)
                announced[net_id] = (uuid, struct.unpack_from("<3d", body, 4 + 40))
                if a.enemy_record:
                    record(body)
            elif msg_type == ENEMY_GONE and a.enemy_hit:
                gone_id = struct.unpack_from('<H', body, 4)[0]
                announced.pop(gone_id, None)  # a dead or despawned enemy is not hit again
                if body[6] == ENEMY_GONE_DIED:
                    print(f"enemy: ENEMY_GONE(Died) for net id {gone_id}", flush=True)
                if a.enemy_record:
                    record(body)
            elif msg_type in (ENEMY_STATE, ENEMY_GONE, ENEMY_ANIM) and a.enemy_record:
                record(body)
            elif msg_type == CARGO_LIST and a.echo_cargo:
                sock.sendall(encode(CARGO_LIST, peer_slot, body[4:]))
            elif msg_type == EQUIP_STATE and a.echo_equip:
                sock.sendall(encode(EQUIP_STATE, peer_slot, body[4:]))
            elif msg_type in (WEAPON_STATE, WEAPON_FIRE) and a.echo_weapon:
                sock.sendall(encode(msg_type, peer_slot, body[4:]))
            elif msg_type == ANIM_STATE and a.echo_anim:
                sock.sendall(encode(ANIM_STATE, peer_slot, body[4:], flags=0))

    threading.Thread(target=receive, daemon=True).start()
    seq, start, last_hb, centre = 0, None, 0.0, None
    last_held, last_equip, last_env = None, 0.0, 0.0
    hit_target = None  # the net id --enemy-hit keeps hitting
    enemies_listed = False

    def far(net_id):
        at = announced[net_id][1]
        return sum((at[i] - local[axis]) ** 2 for i, axis in enumerate("xyz")) > ENEMY_REACH_METRES ** 2
    struct_added = struct_removed = False
    gave = picked = gave_plain = told_story = False
    last_load = 0.0
    replay = load_enemy_recording(a.enemy_replay) if a.enemy_replay else []
    replay_start = None
    sent_enemy = [0]
    last_hit = 0.0
    last_weapon = last_fire = 0.0
    struct_sent = 0
    last_bt = 0.0
    while True:
        now = time.monotonic()
        if now - last_hb >= 1.0:
            last_hb = now
            sock.sendall(encode(HEARTBEAT, HOST_SLOT, flags=0))
        if local and (centre is None or a.follow):
            fx, fy = math.sin(local["yaw"]), math.cos(local["yaw"])  # forward (yaw = atan2(forward.x, forward.y))
            first = centre is None
            centre = (local["x"] + fx * a.offset + a.radius, local["y"] + fy * a.offset, local["z"])
            if first:
                start = now
                print(f"circle centre {centre}", flush=True)
        if a.struct and local and start is not None:
            add_at, remove_at = map(float, a.struct.split(","))
            if not struct_added and now - start >= add_at:
                struct_added = True
                sock.sendall(encode(STRUCT_CREATE, peer_slot, ladder_payload(local, a.struct_kind)))
                print("struct: ladder replayed", flush=True)
            if struct_added and not struct_removed and now - start >= remove_at:
                struct_removed = True
                sock.sendall(encode(STRUCT_REMOVE, peer_slot, struct.pack("<IBBBB", LADDER_ID, 1, 0, 0, 0)))
                print("struct: ladder removed", flush=True)
        if a.give_order and start is not None and not gave and now - start >= float(a.give_order):
            gave = True
            sock.sendall(encode(CARGO_ADD, peer_slot, CARGO_ADD_FORMAT.pack(641900174, 7, 900.0, 0, 0x1000071000018E, 0)))
            print("cargo: gave the order piece", flush=True)
        if a.host_picks_order and start is not None and not picked and now - start >= float(a.host_picks_order):
            picked = True
            sock.sendall(encode(0x0106, peer_slot, struct.pack("<IIfffIQ", 0, 641900174, 0.0, 0.0, 0.0, 0, 0x1000071000018E)))
            print("cargo: host picked up the order piece", flush=True)
        if a.give_plain and start is not None and not gave_plain and now - start >= float(a.give_plain):
            gave_plain = True
            sock.sendall(encode(CARGO_ADD, peer_slot, CARGO_ADD_FORMAT.pack(306656069, 0, 123.0, 0, 0, 0)))
            print("cargo: gave a worn plain piece", flush=True)
        if a.story and start is not None and not told_story:
            seconds, kind, mission = a.story.split(":")
            if now - start >= float(seconds):
                told_story = True
                sock.sendall(encode(0x0119, peer_slot, struct.pack("<B3xIiIQ16s", int(kind), 0, -1, 0, int(mission, 16), bytes(16))))
                print("story: event sent", flush=True)
        if replay and start is not None and now - start >= a.enemy_delay:
            if replay_start is None:
                replay_start = now
                print("enemy: replay started", flush=True)
            while replay and replay[0][0] <= now - replay_start:
                _, msg_type, payload = replay.pop(0)
                sent_enemy[0] += 1
                if sent_enemy[0] in (1, 2, 500, 1000):
                    print(f"enemy: sent {sent_enemy[0]} messages, last type {msg_type:#x} {len(payload)} bytes", flush=True)
                sock.sendall(encode(msg_type, peer_slot, payload, flags=0 if msg_type in (ENEMY_STATE, ENEMY_ANIM) else FLAG_RELIABLE))
                if not replay:
                    print("enemy: the recording has been replayed", flush=True)
        if a.enemy_hit and start is not None and announced and now - start >= float(a.enemy_hit.split(":")[0]) and now - last_hit >= 10.0:
            last_hit = now
            if not enemies_listed:
                enemies_listed = True
                for n in sorted(announced, key=lambda n: sum((announced[n][1][i] - local[axis]) ** 2 for i, axis in enumerate("xyz")))[:8]:
                    at = announced[n][1]
                    print(f"enemy: {n} at {at[0] - local['x']:+.1f} east, {at[1] - local['y']:+.1f} north, {at[2] - local['z']:+.1f} up", flush=True)
            if a.enemy_id in announced:
                hit_target = a.enemy_id
            elif hit_target not in announced or far(hit_target):  # stay on one enemy until it is gone or out of reach
                hit_target = min(announced, key=lambda n: sum((announced[n][1][i] - local[axis]) ** 2 for i, axis in enumerate("xyz")))
            net_id, (uuid, _) = hit_target, announced[hit_target]
            amount = float(a.enemy_hit.split(":")[1])
            sock.sendall(encode(ENEMY_HIT, peer_slot, ENEMY_HIT_FORMAT.pack(net_id, 0, uuid, amount, 0, -1, 0, *([0.0] * 12))))
            at = announced[net_id][1]
            print(f"enemy: hit enemy {net_id} for {amount}: {at[0] - local['x']:+.1f} m east, {at[1] - local['y']:+.1f} m north, {at[2] - local['z']:+.1f} m up of the local player", flush=True)
        if a.weapon and local and start is not None:
            weapon_id, weapon_kind, pellets = (int(v) for v in a.weapon.split(":"))
            if now - last_weapon >= WEAPON_RESEND_SECONDS:
                last_weapon = now
                sock.sendall(encode(WEAPON_STATE, peer_slot, WEAPON_STATE_FORMAT.pack(weapon_id, 1, 0)))
            if a.fire > 0 and now - last_fire >= a.fire:
                last_fire = now
                origin = (local["x"], local["y"], local["z"] + MUZZLE_HEIGHT_METRES)
                aim = (1.0, 0.0, 0.0)
                if hit_target in announced:
                    at = announced[hit_target][1]
                    delta = (at[0] - origin[0], at[1] - origin[1], at[2] - origin[2])
                    length = max(sum(c * c for c in delta) ** 0.5, 1e-6)
                    aim = tuple(c / length for c in delta)
                sock.sendall(encode(WEAPON_FIRE, peer_slot, WEAPON_FIRE_FORMAT.pack(weapon_id, weapon_kind, 0, pellets, *origin, *aim)))
                print(f"weapon: fired {weapon_id} toward {aim[0]:+.2f} {aim[1]:+.2f} {aim[2]:+.2f}", flush=True)
        if a.bt_regions and now - last_bt >= 1.0:
            last_bt = now
            sock.sendall(encode(0x0128, peer_slot, struct.pack("<Q", int(a.bt_regions, 16))))
        if a.struct_all and local and start is not None and now - start >= a.struct_all + 8.0 * struct_sent:
            if struct_sent < len(STRUCT_KINDS):
                kind = sorted(STRUCT_KINDS)[struct_sent]
                sock.sendall(encode(STRUCT_CREATE, peer_slot, kind_payload(local, kind, struct_sent)))
                print(f"struct: kind {kind} sent", flush=True)
                struct_sent += 1
        if a.env and now - last_env >= 1.0:
            last_env = now
            fields = a.env.split(",")
            types = [0xE] * 64
            for item in fields[2:]:
                region, kind = item.split(":")
                types[int(region)] = int(kind)
            sock.sendall(encode(WORLD_ENV, peer_slot, ENV.pack(0, 0, float(fields[0]), int(fields[1]) if len(fields) > 1 else 1, 100.0, 200.0, *types)))
        if centre and a.equip:
            begin, end = map(float, a.equip_window.split(","))
            held = begin <= now - start < end
            if held != last_held or now - last_equip >= 5.0:
                last_held, last_equip = held, now
                pieces = [tuple(int(v) for v in item.split(":")) for item in a.equip.split(",")] if held else []
                body = struct.pack("<II", len(pieces), 0) + b"".join(EQUIP_ENTRY.pack(slot, kind) for slot, kind in pieces)
                sock.sendall(encode(EQUIP_STATE, peer_slot, body))
                print(f"equip: peer holds {pieces}", flush=True)
        if centre and a.drive is not None:
            begin, end = map(float, a.drive_window.split(","))
            if begin <= now - start < end:
                x, y, z = map(float, a.drive_pos.split(","))
                seq += 1
                if a.drive_load and now - last_load >= 5.0:
                    last_load = now
                    kinds = [int(k) for k in a.drive_load.split(",")]
                    sock.sendall(encode(VEHICLE_LOAD, peer_slot, VEHICLE_LOAD_HEADER.pack(a.drive, len(kinds), 0) + struct.pack(f"<{len(kinds)}I", *kinds)))
                sock.sendall(encode(VEHICLE_STATE, peer_slot, VEHICLE.pack(seq, a.drive_role, a.drive, x, y, z, 1, 0, 0, 0, 1, 0, 0, 0, 1), flags=0))
        if centre:
            t = max(0.0, now - start - a.hold)
            angle = math.radians(a.phase) + t * a.speed / a.radius
            x = centre[0] + a.radius * math.cos(angle) - a.radius
            y = centre[1] + a.radius * math.sin(angle)
            yaw = math.atan2(-math.sin(angle), math.cos(angle)) if t > 0 else 0.0
            if a.follow and local:  # beside the local player: --offset ahead, --radius to its right, facing the same way
                fx, fy = math.sin(local["yaw"]), math.cos(local["yaw"])
                x = local["x"] + fx * a.offset + fy * a.radius
                y = local["y"] + fy * a.offset - fx * a.radius
                yaw = local["yaw"]
            seq += 1
            sock.sendall(encode(PLAYER_STATE, peer_slot, STATE.pack(seq, x, y, centre[2], yaw, 0), flags=0))
        time.sleep(1.0 / SEND_HZ)


if __name__ == "__main__":
    main()
