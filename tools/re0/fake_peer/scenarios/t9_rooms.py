"""T9 rooms: the fake answers the game's TEAM doors (commit), runs its own doors from the door library, races a door, moves into
rooms the game has named (leave / enter), probes the self-heal and, as a replay authority, leaves a room mid-fight."""
from fake_peer import wire
from fake_peer.gates import MISSING, PASS, Latency, Result, Wire, forbid
from fake_peer.library import OUT
from fake_peer.scenarios.common import enter_game_room, gate, line, newest_clip, run_room_by_replay

ID = "t9_rooms"
ROWS = "T9 R2, R2-busy, R3, self-heal, X5"
SEATS = ("guest",)
SCRATCH_SEATS = ()
SUMMARY = "doors committed and run from the library, a door race, leave / enter moves, self-heal probe, authority leaving a fight"
DOORS = 10
DOORS_WAIT_S = 900.0
OWN_DOORS_WAIT_S = 120.0
RACE_WAIT_S = 120.0
MOVE_GAP_S = 3.0
HEAL_WAIT_S = 3.0
LEAVE_MAX_MS = 500.0
LOAD_WAIT_S = 600.0
HITS_TO_KILL = 3


def move_latency_start(evidence):
    moves = evidence.journal.frames(wire.Msg.ROOM_MOVE, OUT, evidence.mark("moves", 0.0))
    return moves[0].t if moves else None


def own_doors_ran(evidence):
    rooms = evidence.mirrors["rooms"]
    return Result(PASS if rooms.results else MISSING, f"{len(rooms.results)} answers to the fake's own doors")


def game_doors_answered(evidence):
    rooms = evidence.mirrors["rooms"]
    return Result(PASS if rooms.answered else MISSING, f"{len(rooms.answered)} of the game's doors answered")


def gates(seat):
    return [
        gate("T9.R2", "the game's doors are accepted",
             line(r"door_follow: serial \d+ sent", "door_follow: serial %u sent"),
             line(r"door_follow: serial \d+ partner accepted", "door_follow: serial %u partner accepted"),
             Wire("game doors", game_doors_answered),
             forbid=[forbid(r"room_gate: door into", "room_gate: door into scene 0x%02x waits for the peer's")]),
        gate("T9.R2-own", "the fake's own doors are followed",
             line(r"door_follow: serial \d+ accepted", "door_follow: serial %u accepted"),
             line(r"door_follow: serial \d+ arrived in scene 0x[0-9a-f]+, standing behind", "door_follow: serial %u arrived in scene 0x%02x%s"),
             Wire("own doors", own_doors_ran)),
        gate("T9.R2-busy", "a door race is busy on both sides",
             line(r"door_follow: serial \d+ rejected \(busy\)", "door_follow: serial %u rejected (%s)", after="race"),
             line(r"door_follow: serial \d+ partner rejected \(busy\)", "door_follow: serial %u partner rejected (%s)", after="race")),
        gate("T9.R3", "leave and enter follow the fake's moves quickly",
             line(r"room_move: Billy -> scene 0x[0-9a-f]+ entry \d+ \(leave\)", "room_move: %s -> scene 0x%02x entry %u (%s) serial %u", after="moves"),
             line(r"room_move: Billy -> scene 0x[0-9a-f]+ entry \d+ \(enter\)", "room_move: %s -> scene 0x%02x entry %u (%s) serial %u", after="moves"),
             Latency("ROOM_MOVE to log", r"room_move: Billy -> scene", move_latency_start, LEAVE_MAX_MS,
                     "room_move: %s -> scene 0x%02x entry %u (%s) serial %u"),
             forbid=[forbid(r"SLOW_FRAME", "SLOW_FRAME", after="moves")]),
        gate("T9.self-heal", "self-heal only in the probe",
             line(r"room_move: Billy self-heal to scene", "room_move: %s self-heal to scene 0x%02x", after="heal"),
             forbid=[forbid(r"room_move: Billy self-heal", "room_move: %s self-heal to scene 0x%02x", until="heal")]),
        gate("T9.X5", "the authority leaving a fight hands the enemies over",
             line(r"enemy_observer: authority -> here \(scene 0x[0-9a-f]+ e\d+\), \d+ enemies resumed",
                  "enemy_observer: authority -> here (scene 0x%02x e%u), %u enemies resumed (%u at step 0)")),
    ]


def run(ctx):
    ctx.step("Load a save in TEAM mode")
    if not enter_game_room(ctx, timeout=LOAD_WAIT_S):
        return
    ctx.step(f"Take {DOORS} doors")
    ctx.wait_until(lambda: len(ctx.rooms.game_doors) >= DOORS, ctx.duration(DOORS_WAIT_S), "ten doors")
    own_doors(ctx)
    ctx.step("Press a door at once when the fake does")
    ctx.rooms.race = True
    ctx.mark("race")
    ctx.wait_until(lambda: ctx.rooms.results, RACE_WAIT_S, "an answer to the race door")
    ctx.rooms.race = False
    move_through_rooms(ctx)
    replay_authority_leaves(ctx)


def own_doors(ctx):
    with ctx.lock:
        source = ctx.remote.pose.room if ctx.remote.pose else None
        ran = source is not None and ctx.rooms.run_door(source)
    if not ran:
        ctx.require("door", "T9.R2-own")
        return
    ctx.wait_until(lambda: ctx.rooms.results, OWN_DOORS_WAIT_S, "the answer to the fake's own door")


def move_through_rooms(ctx):
    ctx.step("Stand still while the fake leaves and enters rooms")
    ctx.mark("moves")
    seen = {m for m in ctx.rooms.known | ctx.rooms.library_scenes() if m != ctx.peer.place.scene}
    for scene in sorted(seen):
        with ctx.lock:
            ctx.rooms.move(scene)
        ctx.wait(MOVE_GAP_S)
    with ctx.lock:
        home = ctx.remote.pose.room
        ctx.rooms.move(home)
    ctx.wait(MOVE_GAP_S)
    ctx.mark("heal")
    other = next((s for s in sorted(seen) if s != home), None)
    if other is not None:
        with ctx.lock:
            ctx.rooms.probe_self_heal(other)
        ctx.wait(HEAL_WAIT_S)
        with ctx.lock:
            ctx.rooms.move(home)


def replay_authority_leaves(ctx):
    newest = newest_clip(ctx.library)
    if newest is None:
        ctx.require("enemy clip", "T9.X5")
        return
    (scene, _), clip = newest
    ctx.step("Leave the room; wait for the recorded fight")
    run_room_by_replay(ctx, scene, clip, HITS_TO_KILL)
    ctx.wait_until(lambda: ctx.remote.in_room(scene), LOAD_WAIT_S, "the game in the recorded room")
    ctx.wait(HEAL_WAIT_S)
    with ctx.lock:
        ctx.enemies.stop_replay()
        other = next((s for s in sorted(ctx.rooms.known) if s != scene), None)
        if other is not None:
            ctx.rooms.move(other)
    ctx.wait(MOVE_GAP_S)
