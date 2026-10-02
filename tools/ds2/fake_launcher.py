"""Stand-in DS2 launcher with a scripted second player, so the adapter gets a peer without real launchers (which reset
RE0's adapter.ini). Listens on the adapter port, answers HELLO with WELCOME + PEER_UP, then sends the peer's
PLAYER_STATE (slot 1) at 60 Hz: still for --hold seconds, then walking a circle of --radius m at --speed m/s. The
circle's centre is --offset m ahead of the first local PLAYER_STATE (--phase: the start angle, degrees; --speed 0 keeps
the peer where it starts, e.g. --offset 2 --radius 1.2 --phase 270: 2 m ahead and 1.2 m to the local player's right).
--drive ID (hex) reports the peer in that vehicle (--drive-role 0 driving, 1 riding along), parked at --drive-pos x,y,z with an upright orientation,
between the seconds --drive-window START,END after the first local state: the adapter's remote body boards the vehicle
when the reports start and leaves when they stop. Prints the local player's pose.

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
    (length,) = struct.unpack("<I", read_exact(sock, 4))
    read_exact(sock, length)
    sock.sendall(encode(WELCOME, HOST_SLOT, struct.pack("<BBBI", HOST_SLOT, HOST_SLOT, MAX_PLAYERS, EPOCH)))
    name = b"fake peer"
    sock.sendall(encode(PEER_UP, HOST_SLOT, struct.pack("<BQB", PEER_SLOT, 0x1100001DEADBEEF, len(name)) + name))
    print("adapter linked", flush=True)
    local = {}

    def receive():
        last_print = 0.0
        while True:
            (n,) = struct.unpack("<I", read_exact(sock, 4))
            body = read_exact(sock, n)
            msg_type = struct.unpack_from("<H", body)[0]
            if msg_type == PLAYER_STATE and len(body) >= 4 + STATE.size:
                _, x, y, z, yaw, _ = STATE.unpack_from(body, 4)
                local.update(x=x, y=y, z=z, yaw=yaw)
                if time.monotonic() - last_print > 5:
                    last_print = time.monotonic()
                    print(f"local ({x:.2f}, {y:.2f}, {z:.2f}) yaw {yaw:.2f}", flush=True)

    threading.Thread(target=receive, daemon=True).start()
    seq, start, last_hb, centre = 0, None, 0.0, None
    while True:
        now = time.monotonic()
        if now - last_hb >= 1.0:
            last_hb = now
            sock.sendall(encode(HEARTBEAT, HOST_SLOT, flags=0))
        if local and centre is None:
            fx, fy = math.sin(local["yaw"]), math.cos(local["yaw"])  # forward (yaw = atan2(forward.x, forward.y))
            centre = (local["x"] + fx * a.offset + a.radius, local["y"] + fy * a.offset, local["z"])
            start = now
            print(f"circle centre {centre}", flush=True)
        if centre and a.drive is not None:
            begin, end = map(float, a.drive_window.split(","))
            if begin <= now - start < end:
                x, y, z = map(float, a.drive_pos.split(","))
                seq += 1
                sock.sendall(encode(VEHICLE_STATE, PEER_SLOT, VEHICLE.pack(seq, a.drive_role, a.drive, x, y, z, 1, 0, 0, 0, 1, 0, 0, 0, 1), flags=0))
        if centre:
            t = max(0.0, now - start - a.hold)
            angle = math.radians(a.phase) + t * a.speed / a.radius
            x = centre[0] + a.radius * math.cos(angle) - a.radius
            y = centre[1] + a.radius * math.sin(angle)
            yaw = math.atan2(-math.sin(angle), math.cos(angle)) if t > 0 else 0.0
            seq += 1
            sock.sendall(encode(PLAYER_STATE, PEER_SLOT, STATE.pack(seq, x, y, centre[2], yaw, 0), flags=0))
        time.sleep(1.0 / SEND_HZ)


if __name__ == "__main__":
    main()
