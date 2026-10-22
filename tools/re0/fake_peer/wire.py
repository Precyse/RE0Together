"""Every message id and payload layout the fake peer speaks (one table), the frame codec and the variable-size helpers.

Layouts mirror the C++ structs of adapters/re0/src (wire_check.py compares sizes and ids with the headers, the golden
frames pin the bytes). Little endian, no implicit padding: every pad byte is written out in the format.
"""
import enum
import socket
import struct
from collections import namedtuple

PROTO_VERSION = 1
FLAG_RELIABLE = 1
SLOT_ALL = 0xFF
NO_OWNER = 0xFF
FIRST_GAME_TYPE = 0x0100
FIRST_STATE_TYPE = 0x0140
LAST_STATE_TYPE = 0x017F
MAX_FRAME_LEN = 1 << 20
SCENE_LIMIT = 0xAF
NO_SCENE = 0xFFFF
NO_TARGET = 0xFF
NO_MOTION = 0xFFFF
BILLY, REBECCA = 0, 1
CHARACTER_NAMES = ("billy", "rebecca")
HOST_SLOT = 0
FLAG_WORDS = 0x47


class Msg(enum.IntEnum):
    HELLO = 0x0001
    WELCOME = 0x0002
    PEER_UP = 0x0003
    PEER_DOWN = 0x0004
    REJECT = 0x0005
    HEARTBEAT = 0x0020
    PLAYER_STATE = 0x0100
    PAD_FRAME = 0x0101
    OWNERSHIP = 0x0102
    ROOM_STATE = 0x0104
    MENU_STATE = 0x0105
    FLOOR_PUT = 0x0107
    FLOOR_TAKE = 0x0108
    PARTY_REQUEST = 0x0109
    PARTY_MODE = 0x010A
    DOOR_CHANGE = 0x010B
    SNAPSHOT_REQUEST = 0x010D
    JOIN_SNAPSHOT = 0x010E
    SAVE_SLOT = 0x010F
    HIT_REQUEST = 0x0110
    HIT_APPLIED = 0x0111
    RESYNC_REQUEST = 0x0114
    FLOOR_SNAPSHOT = 0x0115
    APPEARANCE = 0x0116
    PLAYER_DIED = 0x0120
    EVENT_START = 0x0130
    EVENT_STEPS = 0x0131
    POSE_STATE = 0x0140
    MOTION_EVENT = 0x0141
    WEAPON_FX = 0x0142
    ROOM_MOVE = 0x0150
    DOOR_FOLLOW = 0x0151
    DOOR_FOLLOW_RESULT = 0x0152
    DEMO_SKIP_REQUEST = 0x0153
    DEMO_SKIP = 0x0154
    SESSION_READY = 0x0155
    JOIN_EXTRAS = 0x0156
    ENEMY_POSE = 0x0160
    ENEMY_MOTION = 0x0161
    ENEMY_LEASE = 0x0163
    ENEMY_LEASE_END = 0x0164
    PLAYER_HIT = 0x0165
    ROOM_CLAIM = 0x0166
    ROOM_DECLINE = 0x0167
    ROOM_STOP = 0x0168
    BOSS_HANDOVER = 0x0169
    TAKE_REQUEST = 0x0170
    TAKE_RESULT = 0x0171
    PROP_LEASE = 0x0172
    PROP_MOVE = 0x0173
    PROP_COMMIT = 0x0174
    FLAG_PROPOSE = 0x0175
    FLAG_COMMIT = 0x0176


# The C++ constant that carries each id: (header under adapters/re0/src or adapters/ds2/src, constant). Messages whose
# id the adapter spells as an offset of another constant are listed by the constant that defines them.
CPP_IDS = {
    Msg.PLAYER_STATE: ("state_sync.h", "kMsgPlayerState"),
    Msg.ROOM_STATE: ("protocol.h", "kMsgRoomState"),
    Msg.FLOOR_PUT: ("protocol.h", "kMsgFloorPut"),
    Msg.FLOOR_TAKE: ("protocol.h", "kMsgFloorTake"),
    Msg.SNAPSHOT_REQUEST: ("protocol.h", "kMsgSnapshotRequest"),
    Msg.JOIN_SNAPSHOT: ("protocol.h", "kMsgJoinSnapshot"),
    Msg.SAVE_SLOT: ("protocol.h", "kMsgSaveSlot"),
    Msg.HIT_REQUEST: ("enemy_protocol.h", "kMsgHitRequest"),
    Msg.RESYNC_REQUEST: ("protocol.h", "kMsgResyncRequest"),
    Msg.FLOOR_SNAPSHOT: ("protocol.h", "kMsgFloorSnapshot"),
    Msg.PLAYER_DIED: ("player_damage.h", "kMsgPlayerDied"),
    Msg.OWNERSHIP: ("character_owner.h", "kMsgOwnership"),
    Msg.PARTY_MODE: ("protocol.h", "kMsgPartyMode"),
    Msg.POSE_STATE: ("pose_wire.h", "kMsgPoseState"),
    Msg.MOTION_EVENT: ("pose_wire.h", "kMsgMotionEvent"),
    Msg.WEAPON_FX: ("weapon_fx_wire.h", "kMsgWeaponFx"),
    Msg.ROOM_MOVE: ("room_move_wire.h", "kMsgRoomMove"),
    Msg.DOOR_FOLLOW: ("room_move_wire.h", "kMsgDoorFollow"),
    Msg.DOOR_FOLLOW_RESULT: ("room_move_wire.h", "kMsgDoorFollowResult"),
    Msg.DEMO_SKIP_REQUEST: ("demo_skip_wire.h", "kMsgDemoSkipRequest"),
    Msg.DEMO_SKIP: ("demo_skip_wire.h", "kMsgDemoSkip"),
    Msg.SESSION_READY: ("session_wire.h", "kMsgSessionReady"),
    Msg.JOIN_EXTRAS: ("session_wire.h", "kMsgJoinExtras"),
    Msg.ENEMY_POSE: ("enemy_observer_wire.h", "kMsgEnemyPose"),
    Msg.ENEMY_MOTION: ("enemy_observer_wire.h", "kMsgEnemyMotion"),
    Msg.ENEMY_LEASE: ("enemy_lease_wire.h", "kMsgEnemyLease"),
    Msg.ENEMY_LEASE_END: ("enemy_lease_wire.h", "kMsgEnemyLeaseEnd"),
    Msg.PLAYER_HIT: ("enemy_lease_wire.h", "kMsgPlayerHit"),
    Msg.ROOM_CLAIM: ("room_authority_wire.h", "kMsgClaim"),
    Msg.ROOM_DECLINE: ("room_authority_wire.h", "kMsgDecline"),
    Msg.ROOM_STOP: ("room_authority_wire.h", "kMsgStop"),
    Msg.BOSS_HANDOVER: ("boss_wire.h", "kMsgBossHandover"),
    Msg.TAKE_REQUEST: ("item_wire.h", "kMsgTakeRequest"),
    Msg.TAKE_RESULT: ("item_wire.h", "kMsgTakeResult"),
    Msg.PROP_LEASE: ("prop_wire.h", "kMsgLease"),
    Msg.PROP_MOVE: ("prop_wire.h", "kMsgMove"),
    Msg.PROP_COMMIT: ("prop_wire.h", "kMsgCommit"),
    Msg.FLAG_PROPOSE: ("flag_sequencer_wire.h", "kMsgFlagPropose"),
    Msg.FLAG_COMMIT: ("flag_sequencer_wire.h", "kMsgFlagCommit"),
}


def type_name(msg_type):
    try:
        return Msg(msg_type).name
    except ValueError:
        return f"0x{msg_type:04x}"


def is_state_type(msg_type):
    return FIRST_STATE_TYPE <= msg_type <= LAST_STATE_TYPE


class WireError(ValueError):
    """A payload that does not fit its layout."""


LAYOUTS = []


class Layout:
    """A fixed payload: `names` lists the unpacked values in order, `name*N` groups N values into a tuple."""

    def __init__(self, name, header, fmt, names):
        self.name = name
        self.header = header
        self.struct = struct.Struct(fmt)
        self._fields = []
        for item in names.split():
            field, _, count = item.partition("*")
            self._fields.append((field, int(count) if count else 1, bool(count)))
        flat = sum(count for _, count, _ in self._fields)
        if flat != len(self.struct.unpack(bytes(self.struct.size))):
            raise ValueError(f"{name}: {flat} names for a different number of values")
        self.record = namedtuple(name, [field for field, _, _ in self._fields])
        self.size = self.struct.size
        LAYOUTS.append(self)

    def pack(self, **values):
        unknown = set(values) - {field for field, _, _ in self._fields}
        if unknown:
            raise WireError(f"{self.name}: unknown field {sorted(unknown)}")
        flat = []
        for field, count, grouped in self._fields:
            value = values.get(field, (0,) * count if grouped else 0)
            flat.extend(value if grouped else (value,))
        try:
            return self.struct.pack(*flat)
        except struct.error as error:
            raise WireError(f"{self.name}: {error}") from error

    def unpack(self, data, offset=0):
        if offset == 0 and len(data) != self.size:
            raise WireError(f"{self.name}: {len(data)} bytes, expected {self.size}")
        try:
            flat = self.struct.unpack_from(data, offset)
        except struct.error as error:
            raise WireError(f"{self.name}: {error}") from error
        values, at = [], 0
        for _, count, grouped in self._fields:
            values.append(tuple(flat[at:at + count]) if grouped else flat[at])
            at += count
        return self.record(*values)

    def make(self, **values):
        """The record `pack` would write, for building a message that is then edited with `_replace`."""
        return self.unpack(self.pack(**values))


def _layout(name, header, fmt, names):
    return Layout(name, header, fmt, names)


PlayerState = _layout("PlayerState", "state_sync.h", "<I3f4fBBHiB3x",
                      "seq pos*3 quat*4 character sender_is_host room hp focused")
RoomState = _layout("RoomState", "door_travel.h", "<HBBHBx", "scene partner_in_room enemy_claim door_target door_flags")
Ownership = _layout("Ownership", "character_owner.h", "<BB", "billy_owner rebecca_owner")
SaveSlot = _layout("SaveSlot", None, "<ii", "slot phase")
MenuState = _layout("MenuState", "menu_mirror.h", "<BB", "open phase")
FloorPut = _layout("FloorPut", "floor_items_sync.h", "<HHII3f3f", "room world_epoch item_id count pos*3 rot*3")
FloorTake = _layout("FloorTake", "floor_items_sync.h", "<HHI3f", "room world_epoch item_id pos*3")
FloorHeader = _layout("Header", "floor_snapshot.h", "<HH", "count reserved")
FloorEntry = _layout("Entry", "floor_snapshot.h", "<HBBII3f3f", "room is_take reserved item_id count pos*3 rot*3")
HitRequest = _layout("HitPayload", "enemy_protocol.h", "<4B3f4i2i4IIBB2x4i",
                     "slot attacker flag room offset*3 range_tier attack_type a b hp_before hp_after "
                     "random*4 vtable reacted forwarded reaction*4")

PoseState = _layout("PoseState", "pose_wire.h", "<II3fIfIIHHHHHHh6B",
                    "sender_ms seq pos*3 quat frame0 status x1034 room load_serial motion0 motion1 frame1 hit_attr "
                    "manual_pitch group state phase weapon aim_tier flags")
MotionEvent = _layout("MotionEvent", "pose_wire.h", "<IIffIHH", "sender_ms flags interp start seq motion_no layer")
WeaponFxHeader = _layout("Header", "weapon_fx_wire.h", "<IIBBH", "sender_ms seq character count pad")

RoomMove = _layout("RoomMove", "room_move_wire.h", "<IHHHBB", "seq scene entry load_serial character mode")
DoorFollow = _layout("DoorFollow", "room_move_wire.h", "<IHHIIIIBBH",
                     "serial source_scene scene entry type motion flag character reason reserved")
DoorFollowResult = _layout("DoorFollowResult", "room_move_wire.h", "<IBBH", "serial result character reserved")

SessionReady = _layout("SessionReady", "session_wire.h", "<BBHHHI", "phase reserved scene load_serial reserved2 snapshot_serial")
ExtrasHeader = _layout("ExtrasHeader", "session_wire.h", "<IIHHHH", "snapshot_serial flag_serial claims props items world_epoch")
ItemTake = _layout("ItemTake", "session_wire.h", "<HHI3f", "scene reserved item_id pos*3")
DemoSkip = _layout("DemoSkip", "demo_skip_wire.h", "<HHBB", "demo_id serial flags reserved")

AuthMessage = _layout("AuthMessage", "authority_wire.h", "<QIB3x", "id epoch owner")
EnemyHeader = _layout("Header", "enemy_observer_wire.h", "<IIIHBB", "sender_ms seq room_epoch scene count pad")
EnemyPoseEntry = _layout("PoseEntry", "enemy_observer_wire.h", "<4BHHhH3fIHHfHH4iI",
                         "slot class_index flags target spawn_id spawn_record hp hit_attr pos*3 quat motion0 motion1 "
                         "frame0 frame1 lease_token record*4 flags6a0c")
EnemyMotionEntry = _layout("MotionEntry", "enemy_observer_wire.h", "<BBHIffHH", "slot layer motion_no flags interp start dt_ms seq_low")
EnemyLease = _layout("EnemyLease", "enemy_lease_wire.h", "<IIIHBBHBBBBh4i3fI3f",
                     "token room_epoch sender_ms scene slot class_index spawn_id victim kind level pad hp record*4 pos*3 "
                     "quat point*3")
EnemyLeaseEnd = _layout("EnemyLeaseEnd", "enemy_lease_wire.h", "<IIHBBhH4i3fI",
                        "token room_epoch scene slot reason hp pad record*4 pos*3 quat")
PlayerHit = _layout("PlayerHit", "enemy_lease_wire.h", "<IIIHBBHBBB3x3f",
                    "room_epoch sender_ms seq scene slot class_index spawn_id victim kind level point*3")
BossHeader = _layout("Header", "boss_wire.h", "<IIHBBBBH", "token room_epoch scene phase count alive reason pad")
BossMember = _layout("Member", "boss_wire.h", "<BBHhH3fI4i", "slot class_index spawn_id hp pad pos*3 quat record*4")

TakeRequest = _layout("TakeRequest", "item_wire.h", "<HHII3fHH", "seq scene item_id count pos*3 world_epoch reserved")
TakeResult = _layout("TakeResult", "item_wire.h", "<IHHBBH", "serial seq scene requester answer reserved")
PropLease = _layout("Lease", "prop_wire.h", "<IHBBB3x", "serial scene unit op holder")
PropMove = _layout("Move", "prop_wire.h", "<IHBB3ff", "sender_ms scene kind unit pos*3 yaw")
PropCommit = _layout("Commit", "prop_wire.h", "<IHBB3ffB3x", "serial scene kind unit pos*3 yaw committer")
FlagChange = _layout("Change", "flag_diff.h", "<HHII", "word reserved set clear")
FlagProposeHeader = _layout("ProposeHeader", "flag_sequencer_wire.h", "<HH", "count world_epoch")
FlagCommitHeader = _layout("CommitHeader", "flag_sequencer_wire.h", "<IBBH", "serial origin reserved count")

# Sizes the adapter fixes for payloads that carry no layout here.
JOIN_SNAPSHOT_SIZE = 2 * 56 + 128 + FLAG_WORDS * 4
PLACE_SIZE = 56

# Enumerations the decoders check.
DOOR_RESULTS = ("commit", "busy", "other room", "no peer", "timeout")
RESULT_COMMIT, RESULT_BUSY, RESULT_OTHER_ROOM = range(3)
RESULT_TIMEOUT = len(DOOR_RESULTS) - 1
REASON_TEAM = 0
READY, ECHO = 0, 1
ANSWER_GRANTED, ANSWER_TAKEN, ANSWER_CLAIMED, ANSWER_STALE = 0, 1, 2, 3
ANSWER_TIMED_OUT = 4  # the asking machine's own timeout: never on the wire

# The host's world epoch (world_epoch_rule.h): 1 until a load, never 0; a guest stamps what it sends with the one it adopted.
WORLD_EPOCH_FIRST = 1
LEASE_REQUEST, LEASE_GRANT, LEASE_DENY, LEASE_RELEASE = range(4)
PROP_OBSTACLE_KIND = 0x70
BOSS_PREPARE, BOSS_READY, BOSS_COMMIT, BOSS_OUTCOME, BOSS_CANCEL = range(5)
ENEMY_ALIVE, ENEMY_TERMINAL, ENEMY_REMOVED, ENEMY_LEASED = 1, 2, 4, 8
END_RELEASED, END_ENEMY_DEAD, END_DECLINED, END_EXPIRED, END_SILENT = 1, 2, 4, 5, 6  # contact_rule::End
POSE_FLAG_HELD = 1
AUTO_SPOT = 0xFF
MAX_ENEMY_ENTRIES = 18
MAX_WEAPON_FX_ENTRIES = 16
MAX_BOSS_MEMBERS = 8

QUAT_BITS = 10
QUAT_MAX = (1 << QUAT_BITS) - 1
QUAT_INDEX_SHIFT = 3 * QUAT_BITS
QUAT_RANGE = 0.70710678
IDENTITY_QUAT = (0.0, 0.0, 0.0, 1.0)


def pack_quat(quat):
    """pose_wire::packQuat: smallest three, 2 bits for the dropped component, 10 bits for each of the others."""
    largest = max(range(4), key=lambda i: abs(quat[i]))
    sign = -1.0 if quat[largest] < 0 else 1.0
    packed, shift = largest << QUAT_INDEX_SHIFT, 0
    for i in range(4):
        if i == largest:
            continue
        unit = min(1.0, max(0.0, sign * quat[i] / QUAT_RANGE * 0.5 + 0.5))
        packed |= round(unit * QUAT_MAX) << shift
        shift += QUAT_BITS
    return packed


def unpack_quat(packed):
    largest = packed >> QUAT_INDEX_SHIFT
    quat, squares, shift = [0.0] * 4, 0.0, 0
    for i in range(4):
        if i == largest:
            continue
        quat[i] = (((packed >> shift) & QUAT_MAX) / QUAT_MAX - 0.5) * 2.0 * QUAT_RANGE
        squares += quat[i] * quat[i]
        shift += QUAT_BITS
    quat[largest] = max(0.0, 1.0 - squares) ** 0.5
    return tuple(quat)


IDENTITY_PACKED_QUAT = pack_quat(IDENTITY_QUAT)

Frame = namedtuple("Frame", "type flags slot payload")
_LEN = struct.Struct("<I")
_HEAD = struct.Struct("<HBB")


def encode_frame(msg_type, payload=b"", flags=FLAG_RELIABLE, slot=SLOT_ALL):
    return _LEN.pack(_HEAD.size + len(payload)) + _HEAD.pack(msg_type, flags, slot) + payload


def decode_body(body):
    msg_type, flags, slot = _HEAD.unpack_from(body)
    return Frame(msg_type, flags, slot, bytes(body[_HEAD.size:]))


def read_exact(sock, count):
    chunks = []
    while count:
        chunk = sock.recv(count)
        if not chunk:
            raise ConnectionError("link closed")
        chunks.append(chunk)
        count -= len(chunk)
    return b"".join(chunks)


def read_frame(sock):
    (length,) = _LEN.unpack(read_exact(sock, _LEN.size))
    if length < _HEAD.size or length > MAX_FRAME_LEN:
        raise ConnectionError(f"bad frame length {length}")
    return decode_body(read_exact(sock, length))


def hello_payload(game_id):
    game = game_id.encode("ascii")
    return struct.pack("<HB", PROTO_VERSION, len(game)) + game


WELCOME_FORMAT = struct.Struct("<BBBI")
PEER_UP_FORMAT = struct.Struct("<BQB")


def welcome_payload(local_slot, host_slot, max_players, epoch):
    return WELCOME_FORMAT.pack(local_slot, host_slot, max_players, epoch)


def peer_up_payload(slot, steam_id, name):
    raw = name.encode("utf-8")
    return PEER_UP_FORMAT.pack(slot, steam_id, len(raw)) + raw


def parse_welcome(payload):
    # The launcher appends the local SteamID (15 bytes in all); the fake reads only the first 7.
    local_slot, host_slot, max_players, epoch = WELCOME_FORMAT.unpack_from(payload)
    return {"local_slot": local_slot, "host_slot": host_slot, "max_players": max_players, "epoch": epoch}


# ---- variable-size payloads --------------------------------------------------------------------------------------

def pack_list(header_layout, entry_layout, header_fields, entries):
    """A header (whose `count` is filled in) followed by the packed entries; entries are field dicts or records."""
    body = b"".join(entry if isinstance(entry, bytes) else entry_layout.pack(**entry) for entry in entries)
    return header_layout.pack(**dict(header_fields, count=len(entries))) + body


def unpack_list(header_layout, entry_layout, payload):
    """(header record, [entry records]) of a header-plus-entries payload whose size matches its count exactly."""
    if len(payload) < header_layout.size:
        raise WireError(f"{header_layout.name}: {len(payload)} bytes")
    header = header_layout.unpack(payload[:header_layout.size])
    expected = header_layout.size + header.count * entry_layout.size
    if len(payload) != expected:
        raise WireError(f"{header_layout.name}: {len(payload)} bytes for {header.count} entries, expected {expected}")
    entries = [entry_layout.unpack(payload, header_layout.size + i * entry_layout.size) for i in range(header.count)]
    return header, entries


def pack_enemy_batch(header_fields, entry_layout, entries):
    return pack_list(EnemyHeader, entry_layout, header_fields, entries)


def unpack_enemy_batch(entry_layout, payload):
    return unpack_list(EnemyHeader, entry_layout, payload)


def pack_flag_propose(changes, world_epoch):
    return FlagProposeHeader.pack(count=len(changes), world_epoch=world_epoch) + b"".join(FlagChange.pack(**change) for change in changes)


def unpack_flag_propose(payload):
    header = FlagProposeHeader.unpack(payload[:FlagProposeHeader.size])
    return _unpack_changes(payload, FlagProposeHeader.size, header.count)


def pack_flag_commit(serial, origin, changes):
    return (FlagCommitHeader.pack(serial=serial, origin=origin, count=len(changes)) +
            b"".join(FlagChange.pack(**change) for change in changes))


def unpack_flag_commit(payload):
    header = FlagCommitHeader.unpack(payload[:FlagCommitHeader.size])
    return header, _unpack_changes(payload, FlagCommitHeader.size, header.count)


def _unpack_changes(payload, offset, count):
    if len(payload) != offset + count * FlagChange.size:
        raise WireError(f"flag changes: {len(payload)} bytes for {count}")
    return [FlagChange.unpack(payload, offset + i * FlagChange.size) for i in range(count)]


def pack_extras(snapshot_serial, flag_serial, claims, props, items, world_epoch=WORLD_EPOCH_FIRST):
    header = ExtrasHeader.pack(snapshot_serial=snapshot_serial, flag_serial=flag_serial, claims=len(claims),
                               props=len(props), items=len(items), world_epoch=world_epoch)
    return (header + b"".join(AuthMessage.pack(**c) for c in claims) + b"".join(PropCommit.pack(**p) for p in props) +
            b"".join(ItemTake.pack(**i) for i in items))


def unpack_extras(payload):
    """(header, claims, props, items) of a JOIN_EXTRAS payload."""
    header = ExtrasHeader.unpack(payload[:ExtrasHeader.size])
    expected = (ExtrasHeader.size + header.claims * AuthMessage.size + header.props * PropCommit.size +
                header.items * ItemTake.size)
    if len(payload) != expected:
        raise WireError(f"JOIN_EXTRAS: {len(payload)} bytes, expected {expected}")
    at = ExtrasHeader.size
    parts = []
    for layout, count in ((AuthMessage, header.claims), (PropCommit, header.props), (ItemTake, header.items)):
        parts.append([layout.unpack(payload, at + i * layout.size) for i in range(count)])
        at += count * layout.size
    return (header, *parts)


def pack_floor_snapshot(entries):
    return pack_list(FloorHeader, FloorEntry, {}, entries)


def unpack_floor_snapshot(payload):
    return unpack_list(FloorHeader, FloorEntry, payload)


def pack_boss(header_fields, members):
    return pack_list(BossHeader, BossMember, header_fields, members)


def unpack_boss(payload):
    return unpack_list(BossHeader, BossMember, payload)


# WEAPON_FX entries start with their op; op -> (size, offset of the first position float or None). Op 3 (shoot) is 4
# bytes for a casing or a magazine and 44 for a projectile, whose kind byte (at +2) is 0.
WEAPON_FX_COUNT_OFFSET = 9
WEAPON_FX_SHOOT = 3
WEAPON_FX_PROJECTILE_SIZE = 44
WEAPON_FX_SHOOT_POS = 4
WEAPON_FX_ENTRIES = {1: (8, None), 2: (8, None), 4: (32, 8), 5: (16, 4), 6: (4, None), 7: (4, None)}
WEAPON_FX_ENTRY_PAD = (4, None)


def weapon_fx_entries(payload):
    """(offset, size, position offset or None) of every entry after the header, stopping at an unknown op."""
    at = WeaponFxHeader.size
    for _ in range(payload[WEAPON_FX_COUNT_OFFSET]):
        if at >= len(payload):
            return
        op = payload[at]
        if op == WEAPON_FX_SHOOT:
            size, pos = ((WEAPON_FX_PROJECTILE_SIZE, WEAPON_FX_SHOOT_POS) if payload[at + 2] == 0 else WEAPON_FX_ENTRY_PAD)
        elif op in WEAPON_FX_ENTRIES:
            size, pos = WEAPON_FX_ENTRIES[op]
        else:
            return
        yield at, size, pos
        at += size


def shift_weapon_fx_x(payload, offset_x):
    """The payload with `offset_x` added to the x of every position inside it."""
    data = bytearray(payload)
    for at, _, pos in weapon_fx_entries(payload):
        if pos is not None:
            (x,) = struct.unpack_from("<f", data, at + pos)
            struct.pack_into("<f", data, at + pos, x + offset_x)
    return bytes(data)


def connect(host, port, timeout=5.0):
    sock = socket.create_connection((host, port), timeout=timeout)
    sock.settimeout(None)
    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    return sock
