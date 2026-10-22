"""Helpers the scenarios share: seats and slots, frame counts over the journal, and entering the game's room."""
from fake_peer import wire
from fake_peer.gates import Gate, Line
from fake_peer.library import IN

POSE_WAIT_S = 300.0
LOAD_WAIT_S = 600.0


def fake_slot(seat):
    """The slot the fake plays: the guest is slot 1, the host slot 0."""
    return 1 if seat == "guest" else 0


def game_slot(seat):
    return 1 - fake_slot(seat)


def gate(gate_id, title, *expect, forbid=()):
    return Gate(gate_id, title, expect=expect, forbid=forbid)


def line(pattern, anchor, **options):
    return Line(pattern, anchor, **options)


def frames(evidence, msg_type, direction=IN, since=None, until=None):
    start, end = evidence.window(since, until)
    return evidence.journal.frames(msg_type, direction, start, end)


def fx_entries_received(evidence):
    """The number of WEAPON_FX entries the game sent (the sum of every header's count)."""
    return sum(wire.WeaponFxHeader.unpack(e.payload[:wire.WeaponFxHeader.size]).count
               for e in frames(evidence, wire.Msg.WEAPON_FX) if len(e.payload) >= wire.WeaponFxHeader.size)


def enter_game_room(ctx, timeout=POSE_WAIT_S, source="stand"):
    """Waits for the game's first pose, moves the fake into the game's room and lets it stand there. False if no pose came."""
    if not ctx.wait_until(lambda: ctx.remote.pose is not None, timeout, "the game's first pose"):
        return False
    with ctx.lock:
        ctx.rooms.known.add(ctx.remote.pose.room)
        ctx.rooms.move(ctx.remote.pose.room)
        ctx.sources.use(source)
    return True


STAND_BESIDE_X = 150.0


def newest_clip(library):
    """((scene, load), clip) of the most recently recorded enemy clip, or None."""
    clips = sorted(library.enemy_clips.items(), key=lambda item: item[1]["start"])
    return clips[-1] if clips else None


def first_enemy_position(clip):
    for item in clip["frames"]:
        if item["type"] == wire.Msg.ENEMY_POSE:
            return wire.unpack_enemy_batch(wire.EnemyPoseEntry, item["payload"])[1][0].pos
    return None


def run_room_by_replay(ctx, scene, clip, hits_to_kill):
    """The fake takes a recorded room before the game enters it: moves in, claims it, stands beside the first enemy and replays the clip."""
    with ctx.lock:
        pos = first_enemy_position(clip) or (0.0, 0.0, 0.0)
        ctx.authority.mode = "first"
        ctx.rooms.move(scene)
        ctx.authority.claim_scene(scene)
        ctx.source("stand", pos=(pos[0] + STAND_BESIDE_X, pos[1], pos[2]))
        ctx.enemies.start_replay(scene, clip, hits_to_kill)
