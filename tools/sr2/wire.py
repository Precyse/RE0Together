"""Loopback wire format of the launcher link (docs/CONTRACT.md), shared by the SR2 stand-in tools:
frame = u32 length, u16 type, u8 flags, u8 slot, payload (length counts everything after the length field)."""
import socket
import struct

PROTO = 1
FLAG_RELIABLE = 1
SLOT_ALL = 0xFF
MSG_HELLO = 0x0001
MSG_WELCOME = 0x0002
MSG_PEER_UP = 0x0003
MSG_PEER_DOWN = 0x0004
MSG_HEARTBEAT = 0x0020
MSG_GAME_DATAGRAM = 0x0100
BROADCAST = 0xFFFFFFFF
MAX_DATAGRAM = 1300
PING_BYTES = 20

LEN_FIELD = struct.Struct("<I")
HEADER = struct.Struct("<HBB")
WELCOME = struct.Struct("<BBBIQ")
PEER_UP_FIXED = struct.Struct("<BQB")
DST = struct.Struct("<I")


def encode(msg_type, flags, slot, payload=b""):
    return LEN_FIELD.pack(HEADER.size + len(payload)) + HEADER.pack(msg_type, flags, slot) + payload


def read_exact(sock, n):
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("closed")
        buf += chunk
    return buf


def read_frame(sock):
    """The next frame as (type, flags, slot, payload)."""
    (length,) = LEN_FIELD.unpack(read_exact(sock, LEN_FIELD.size))
    body = read_exact(sock, length)
    msg_type, flags, slot = HEADER.unpack(body[:HEADER.size])
    return msg_type, flags, slot, body[HEADER.size:]


def hello(game_id):
    game = game_id.encode("ascii")
    return encode(MSG_HELLO, FLAG_RELIABLE, 0, struct.pack("<HB", PROTO, len(game)) + game)


def welcome(local_slot, host_slot, max_players, epoch, local_id):
    return encode(MSG_WELCOME, FLAG_RELIABLE, 0, WELCOME.pack(local_slot, host_slot, max_players, epoch, local_id))


def peer_up(slot, steam_id, name):
    text = name.encode("utf-8")
    return encode(MSG_PEER_UP, FLAG_RELIABLE, 0, PEER_UP_FIXED.pack(slot, steam_id, len(text)) + text)


def peer_down(slot):
    return encode(MSG_PEER_DOWN, FLAG_RELIABLE, 0, bytes([slot]))


def datagram(dst, data):
    """A GAME_DATAGRAM payload (u32 dst, then the game's bytes); the game's bytes are 1..1300."""
    if not 1 <= len(data) <= MAX_DATAGRAM:
        raise ValueError(f"datagram of {len(data)} bytes")
    return DST.pack(dst) + data


def split_datagram(payload):
    """(dst, data) of a GAME_DATAGRAM payload."""
    return DST.unpack(payload[:DST.size])[0], payload[DST.size:]


def dotted(address):
    return socket.inet_ntoa(struct.pack(">I", address))
