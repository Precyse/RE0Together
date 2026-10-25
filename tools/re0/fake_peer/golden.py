"""The golden frames file: one accepted and at least one refused sample per state message type, built with wire.py and
decoded by the adapter's own decoders in adapters/re0/tests/fake_peer_wire_test.cpp.

Record: u16 type, u8 expect (1 accepted, 0 refused), u8 0, u32 payload length, payload. Types outside the state block
(PLAYER_STATE, ROOM_STATE, FLOOR_PUT, FLOOR_TAKE, HIT_REQUEST, JOIN_SNAPSHOT) are accepted when the payload has the size
the adapter fixes, refused otherwise; FLOOR_SNAPSHOT goes through floor_snapshot::decode.
"""
import struct
from pathlib import Path

from fake_peer import wire
from fake_peer import wire_check
from fake_peer.enemies import POOL_SLOTS
from fake_peer.wire import Msg

GOLDEN_PATH = Path(__file__).resolve().parent / "tests" / "golden_frames.bin"
RECORD = struct.Struct("<HBBI")
NAN = float("nan")

SCENE = 0x2D
LOAD_SERIAL = 3
ENEMY_CLASS = 0
BOSS_CLASS = wire_check.vtables("kEnemyVtables").index(wire_check.vtables("kBossVtables")[0])
ENEMY_SLOT = 4
POS = (100.0, 5.0, -200.0)
EPOCH = wire.WORLD_EPOCH_FIRST
PLAY_SE_SOUND = 0x1E00
# weapon_fx_sites.h indices: one site per op.
FX_SITES = {"se": 0, "flash": 11, "effect": 13, "casing": 19, "projectile": 21, "stow": 28, "wick": 29, "ripple": 30}
FX_SHOOT_KIND_CASING = 1


def sample(msg_type, expect, payload):
    return (int(msg_type), expect, payload)


def accept(msg_type, payload):
    return sample(msg_type, 1, payload)


def refuse(msg_type, payload):
    return sample(msg_type, 0, payload)


def truncated(payload):
    return payload[:-1]


def weapon_fx(site_override=None, count=None):
    entries = [
        struct.pack("<BBHf", 1, FX_SITES["se"], PLAY_SE_SOUND, 1.0),
        struct.pack("<BBBBf", 2, FX_SITES["flash"], 1, 0, 1.0),
        struct.pack("<BBBB", 3, FX_SITES["casing"], FX_SHOOT_KIND_CASING, 1),
        struct.pack("<BBBB3f4fffI", 3, FX_SITES["projectile"], 0, 0, *POS, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 2),
        struct.pack("<BBHBBbB3f3f", 4, FX_SITES["effect"], 3, 0, 1, -1, 0, *POS, 0.0, 0.0, 0.0),
        struct.pack("<BBBB3f", 5, FX_SITES["ripple"], 0, 0, *POS),
        struct.pack("<BBH", 6, FX_SITES["stow"], 0),
        struct.pack("<BBH", 7, FX_SITES["wick"], 0),
    ]
    if site_override is not None:
        entries[0] = struct.pack("<BBHf", 1, site_override, PLAY_SE_SOUND, 1.0)
    header = wire.WeaponFxHeader.pack(sender_ms=1000, seq=1, character=wire.BILLY,
                                      count=len(entries) if count is None else count)
    return header + b"".join(entries)


def enemy_pose(**fields):
    base = dict(slot=ENEMY_SLOT, class_index=ENEMY_CLASS, flags=wire.ENEMY_ALIVE, target=wire.REBECCA, spawn_id=7,
                spawn_record=2, hp=100, pos=POS, quat=wire.IDENTITY_PACKED_QUAT, record=(2, 5, 0, 0))
    return dict(base, **fields)


def enemy_batch(layout, entry):
    return wire.pack_enemy_batch(dict(sender_ms=1000, seq=1, room_epoch=1, scene=SCENE), layout, [entry])


def boss_member(**fields):
    base = dict(slot=ENEMY_SLOT, class_index=BOSS_CLASS, spawn_id=7, spawn_record=2, hp=500, pos=POS,
                quat=wire.IDENTITY_PACKED_QUAT, record=(2, 5, 0, 0))
    return dict(base, **fields)


def boss_header(**fields):
    return dict(dict(token=(1 << 24) | 1, room_epoch=1, scene=SCENE, phase=wire.BOSS_PREPARE, alive=1), **fields)


def lease_fields(**fields):
    base = dict(token=(1 << 16) | 7, room_epoch=1, sender_ms=1000, scene=SCENE, slot=ENEMY_SLOT, class_index=ENEMY_CLASS,
                spawn_id=7, victim=wire.BILLY, hp=100, record=(2, 5, 0, 0), pos=POS, quat=wire.IDENTITY_PACKED_QUAT)
    return dict(base, **fields)


def extras(claims=(), props=(), items=()):
    return wire.pack_extras(1, 0, list(claims), list(props), list(items))


CLAIM = dict(id=SCENE, epoch=1, owner=1)
PROP_COMMIT = dict(serial=1, scene=SCENE, kind=wire.PROP_OBSTACLE_KIND, unit=2, pos=POS, yaw=0.5, committer=1)
ITEM_TAKE = dict(scene=SCENE, item_id=0x10, pos=POS)
FLAG_CHANGE = dict(word=3, set=0x10, clear=0x01)
BAD_FLAG_CHANGE = dict(word=3, set=0x10, clear=0x10)


def samples():
    pose = wire.PoseState.pack(sender_ms=1000, seq=1, pos=POS, quat=wire.IDENTITY_PACKED_QUAT, room=SCENE,
                               load_serial=LOAD_SERIAL, motion0=wire.NO_MOTION, motion1=wire.NO_MOTION)
    follow = wire.DoorFollow.pack(serial=1, source_scene=SCENE, scene=0x2E, entry=1, character=wire.BILLY)
    snapshot_ready = wire.SessionReady.pack(scene=SCENE, load_serial=LOAD_SERIAL, snapshot_serial=1)
    prepare = wire.pack_boss(boss_header(), [boss_member()])
    digest = wire.WorldDigest.pack(flag_serial=7, flag_crc=0x26B68855, enemy_flag_crc=0x390A7F35, act_flag_crc=0xE58EFF0C,
                                   authority_hash=0xE99D86DE, load_serial=LOAD_SERIAL,
                                   flags=wire.DIGEST_CAUGHT_UP, scene=SCENE, sender_ms=1000)
    return [
        accept(Msg.PLAYER_STATE, bytes(wire.PlayerState.size)),
        refuse(Msg.PLAYER_STATE, bytes(wire.PlayerState.size - 1)),
        accept(Msg.ROOM_STATE, wire.RoomState.pack(scene=SCENE, door_target=wire.NO_SCENE)),
        refuse(Msg.ROOM_STATE, bytes(wire.RoomState.size + 1)),
        accept(Msg.FLOOR_PUT, wire.FloorPut.pack(room=SCENE, world_epoch=EPOCH, item_id=0x10, count=1, pos=POS)),
        refuse(Msg.FLOOR_PUT, bytes(wire.FloorPut.size - 4)),
        accept(Msg.FLOOR_TAKE, wire.FloorTake.pack(room=SCENE, world_epoch=EPOCH, item_id=0x10, pos=POS)),
        refuse(Msg.FLOOR_TAKE, bytes(wire.FloorTake.size + 4)),
        accept(Msg.HIT_REQUEST, bytes(wire.HitRequest.size)),
        refuse(Msg.HIT_REQUEST, bytes(wire.HitRequest.size - 4)),
        accept(Msg.JOIN_SNAPSHOT, bytes(wire.JOIN_SNAPSHOT_SIZE)),
        refuse(Msg.JOIN_SNAPSHOT, bytes(wire.JOIN_SNAPSHOT_SIZE - 4)),
        accept(Msg.FLOOR_SNAPSHOT, wire.pack_floor_snapshot([dict(room=SCENE, item_id=0x10, count=1, pos=POS)])),
        refuse(Msg.FLOOR_SNAPSHOT, truncated(wire.pack_floor_snapshot([dict(room=SCENE, item_id=0x10, count=1)]))),
        accept(Msg.POSE_STATE, pose),
        refuse(Msg.POSE_STATE, truncated(pose)),
        refuse(Msg.POSE_STATE, wire.PoseState.pack(sender_ms=1, seq=1, pos=(NAN, 0.0, 0.0), room=SCENE, load_serial=1)),
        refuse(Msg.POSE_STATE, wire.PoseState.pack(sender_ms=1, seq=1, room=SCENE, load_serial=1, group=4)),
        accept(Msg.MOTION_EVENT, wire.MotionEvent.pack(sender_ms=1000, seq=1, motion_no=0x20, interp=0.5)),
        refuse(Msg.MOTION_EVENT, wire.MotionEvent.pack(sender_ms=1000, seq=1, motion_no=0x20, layer=8)),
        accept(Msg.WEAPON_FX, weapon_fx()),
        refuse(Msg.WEAPON_FX, weapon_fx(count=0)),
        refuse(Msg.WEAPON_FX, weapon_fx(site_override=FX_SITES["flash"])),
        accept(Msg.ROOM_MOVE, wire.RoomMove.pack(seq=1, scene=SCENE, entry=1, load_serial=LOAD_SERIAL, mode=wire.AUTO_SPOT)),
        refuse(Msg.ROOM_MOVE, wire.RoomMove.pack(seq=1, scene=SCENE, entry=1, load_serial=0)),
        refuse(Msg.ROOM_MOVE, wire.RoomMove.pack(seq=1, scene=SCENE, entry=16, load_serial=1)),
        accept(Msg.DOOR_FOLLOW, follow),
        refuse(Msg.DOOR_FOLLOW, wire.DoorFollow.pack(serial=0, source_scene=SCENE, scene=0x2E)),
        refuse(Msg.DOOR_FOLLOW, wire.DoorFollow.pack(serial=1, source_scene=SCENE, scene=0x2E, reserved=1)),
        accept(Msg.DOOR_FOLLOW_RESULT, wire.DoorFollowResult.pack(serial=1, result=wire.RESULT_BUSY, character=wire.REBECCA)),
        refuse(Msg.DOOR_FOLLOW_RESULT, wire.DoorFollowResult.pack(serial=1, result=wire.RESULT_TIMEOUT + 1)),
        accept(Msg.DEMO_SKIP_REQUEST, wire.DemoSkip.pack(demo_id=4, serial=1, flags=1)),
        refuse(Msg.DEMO_SKIP_REQUEST, wire.DemoSkip.pack(demo_id=4, serial=0)),
        accept(Msg.DEMO_SKIP, wire.DemoSkip.pack(demo_id=4, serial=1)),
        refuse(Msg.DEMO_SKIP, wire.DemoSkip.pack(demo_id=4, serial=1, flags=2)),
        accept(Msg.SESSION_READY, snapshot_ready),
        refuse(Msg.SESSION_READY, wire.SessionReady.pack(scene=wire.SCENE_LIMIT, load_serial=1, snapshot_serial=1)),
        refuse(Msg.SESSION_READY, wire.SessionReady.pack(scene=SCENE, load_serial=1, snapshot_serial=0)),
        accept(Msg.JOIN_EXTRAS, extras()),
        accept(Msg.JOIN_EXTRAS, extras([CLAIM], [PROP_COMMIT], [ITEM_TAKE])),
        refuse(Msg.JOIN_EXTRAS, truncated(extras([CLAIM], [PROP_COMMIT], [ITEM_TAKE]))),
        refuse(Msg.JOIN_EXTRAS, extras([dict(CLAIM, epoch=0)])),
        refuse(Msg.JOIN_EXTRAS, wire.pack_extras(1, 0, [], [], [], world_epoch=0)),
        accept(Msg.ENEMY_POSE, enemy_batch(wire.EnemyPoseEntry, enemy_pose())),
        refuse(Msg.ENEMY_POSE, enemy_batch(wire.EnemyPoseEntry, enemy_pose(slot=POOL_SLOTS))),
        accept(Msg.ENEMY_POSE, enemy_batch(wire.EnemyPoseEntry, enemy_pose(spawn_record=wire.SPAWN_RECORD_LIMIT - 1))),
        refuse(Msg.ENEMY_POSE, enemy_batch(wire.EnemyPoseEntry, enemy_pose(spawn_record=wire.SPAWN_RECORD_LIMIT))),
        refuse(Msg.ENEMY_POSE, enemy_batch(wire.EnemyPoseEntry, enemy_pose(flags=wire.ENEMY_LEASED))),
        accept(Msg.ENEMY_MOTION, enemy_batch(wire.EnemyMotionEntry, dict(slot=ENEMY_SLOT, motion_no=0x10))),
        refuse(Msg.ENEMY_MOTION, enemy_batch(wire.EnemyMotionEntry, dict(slot=ENEMY_SLOT, layer=8))),
        accept(Msg.ENEMY_LEASE, wire.EnemyLease.pack(**lease_fields())),
        refuse(Msg.ENEMY_LEASE, wire.EnemyLease.pack(**lease_fields(token=1 << 16))),
        accept(Msg.ENEMY_LEASE_END, wire.EnemyLeaseEnd.pack(token=(1 << 16) | 7, room_epoch=1, scene=SCENE, slot=ENEMY_SLOT,
                                                            reason=wire.END_RELEASED, hp=90, record=(2, 0, 0, 0), pos=POS)),
        refuse(Msg.ENEMY_LEASE_END, wire.EnemyLeaseEnd.pack(token=7, room_epoch=1, scene=SCENE, slot=ENEMY_SLOT, reason=0)),
        accept(Msg.PLAYER_HIT, wire.PlayerHit.pack(room_epoch=1, sender_ms=1000, seq=1, scene=SCENE, slot=ENEMY_SLOT,
                                                  class_index=ENEMY_CLASS, spawn_id=7, victim=wire.BILLY, kind=0, level=2)),
        refuse(Msg.PLAYER_HIT, wire.PlayerHit.pack(room_epoch=1, scene=SCENE, slot=ENEMY_SLOT, victim=2)),
        accept(Msg.ROOM_CLAIM, wire.AuthMessage.pack(**CLAIM)),
        refuse(Msg.ROOM_CLAIM, wire.AuthMessage.pack(**dict(CLAIM, epoch=0))),
        accept(Msg.ROOM_DECLINE, wire.AuthMessage.pack(**CLAIM)),
        refuse(Msg.ROOM_DECLINE, wire.AuthMessage.pack(**dict(CLAIM, id=wire.SCENE_LIMIT))),
        accept(Msg.ROOM_STOP, wire.AuthMessage.pack(**CLAIM)),
        refuse(Msg.ROOM_STOP, wire.AuthMessage.pack(**dict(CLAIM, owner=wire.NO_OWNER))),
        accept(Msg.BOSS_HANDOVER, prepare),
        accept(Msg.BOSS_HANDOVER, wire.pack_boss(boss_header(phase=wire.BOSS_READY, alive=0), [])),
        refuse(Msg.BOSS_HANDOVER, wire.pack_boss(boss_header(phase=wire.BOSS_READY, alive=0), [boss_member()])),
        refuse(Msg.BOSS_HANDOVER, wire.pack_boss(boss_header(), [boss_member(class_index=ENEMY_CLASS)])),
        refuse(Msg.BOSS_HANDOVER, wire.pack_boss(boss_header(), [boss_member(spawn_record=wire.SPAWN_RECORD_LIMIT)])),
        accept(Msg.TAKE_REQUEST, wire.TakeRequest.pack(seq=1, scene=SCENE, item_id=0x10, count=1, pos=POS, world_epoch=EPOCH)),
        refuse(Msg.TAKE_REQUEST, wire.TakeRequest.pack(seq=1, scene=SCENE, item_id=0x100, count=1, pos=POS, world_epoch=EPOCH)),
        accept(Msg.TAKE_RESULT, wire.TakeResult.pack(serial=1, seq=1, scene=SCENE, requester=1, answer=wire.ANSWER_CLAIMED)),
        accept(Msg.TAKE_RESULT, wire.TakeResult.pack(serial=0, seq=1, scene=SCENE, requester=1, answer=wire.ANSWER_STALE)),
        refuse(Msg.TAKE_RESULT, wire.TakeResult.pack(serial=1, seq=1, scene=SCENE, requester=1, answer=wire.ANSWER_TIMED_OUT)),
        accept(Msg.PROP_LEASE, wire.PropLease.pack(scene=SCENE, unit=2, op=wire.LEASE_REQUEST, holder=1)),
        refuse(Msg.PROP_LEASE, wire.PropLease.pack(scene=SCENE, unit=5, op=wire.LEASE_REQUEST)),
        accept(Msg.PROP_MOVE, wire.PropMove.pack(sender_ms=1000, scene=SCENE, kind=wire.PROP_OBSTACLE_KIND, unit=2, pos=POS)),
        refuse(Msg.PROP_MOVE, wire.PropMove.pack(sender_ms=1000, scene=SCENE, kind=0, unit=2, pos=POS)),
        accept(Msg.PROP_COMMIT, wire.PropCommit.pack(**PROP_COMMIT)),
        refuse(Msg.PROP_COMMIT, wire.PropCommit.pack(**dict(PROP_COMMIT, yaw=NAN))),
        accept(Msg.FLAG_PROPOSE, wire.pack_flag_propose([FLAG_CHANGE], EPOCH)),
        refuse(Msg.FLAG_PROPOSE, wire.pack_flag_propose([BAD_FLAG_CHANGE], EPOCH)),
        accept(Msg.FLAG_COMMIT, wire.pack_flag_commit(1, 0, [FLAG_CHANGE])),
        accept(Msg.FLAG_COMMIT, wire.pack_flag_commit(2, 1, [])),
        refuse(Msg.FLAG_COMMIT, wire.pack_flag_commit(1, 0, [dict(FLAG_CHANGE, word=wire.FLAG_WORDS)])),
        accept(Msg.WORLD_DIGEST, digest),
        refuse(Msg.WORLD_DIGEST, truncated(digest)),
    ]


def build():
    return b"".join(RECORD.pack(msg_type, expect, 0, len(payload)) + payload for msg_type, expect, payload in samples())


def write(path=GOLDEN_PATH):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(build())
    return len(samples())


def is_current(path=GOLDEN_PATH):
    return path.exists() and path.read_bytes() == build()
