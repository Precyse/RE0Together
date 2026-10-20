"""T5 enemy observers, the game simulates: the fake stands in the room, validates the game's ENEMY_POSE / ENEMY_MOTION stream
(records it as an enemy clip) and, when a captured hit request exists, shoots a zombie from the non-authority machine."""
from fake_peer.gates import FAIL, PASS, Result, Wire
from fake_peer.scenarios.common import enter_game_room, gate, line

ID = "t5_observe"
ROWS = "T5 enter, X1, X4; T9 hit from non-authority"
SEATS = ("guest",)
SCRATCH_SEATS = ()
SUMMARY = "observer validation of the game's enemy stream (3 slots, epoch, seq, 20 Hz, terminal / removed once); shooter round"
EXPECTED_ENEMIES = 3
LOAD_WAIT_S = 600.0
STREAM_WAIT_S = 30.0
KILL_WAIT_S = 120.0
HIT_WAIT_S = 20.0
LEAVE_S = 30.0


def stream_ok(evidence):
    enemies = evidence.mirrors["enemies"]
    problems = list(enemies.violations)
    if len(enemies.bound_slots()) != EXPECTED_ENEMIES:
        problems.append(f"{len(enemies.bound_slots())} bound slots")
    if not enemies.rate_ok():
        problems.append(f"{enemies.rate_hz():.1f} batches/s")
    problems += [f"slot {n} terminal x{s.terminals}" for n, s in enemies.observed.items() if s.terminals > 1]
    problems += [f"slot {n} removed x{s.removed}" for n, s in enemies.observed.items() if s.removed > 1]
    return Result(FAIL if problems else PASS, "; ".join(problems) or f"{EXPECTED_ENEMIES} slots, {enemies.rate_hz():.1f} batches/s")


def killed_once(evidence):
    terminals = sum(s.terminals for s in evidence.mirrors["enemies"].observed.values())
    return Result(PASS if terminals >= 1 else FAIL, f"{terminals} terminal entries")


def hp_dropped(evidence):
    enemies = evidence.mirrors["enemies"]
    return Result(PASS if enemies.shot_hp_lower() else FAIL, "the next batches carry a lower HP")


def gates(seat):
    return [
        gate("T5.enter", "the game sends its enemies; the stream is valid",
             line(rf"enemy_observer: sending {EXPECTED_ENEMIES} enemies", "enemy_observer: sending %u enemies"),
             Wire("stream", stream_ok)),
        gate("T5.X4", "a kill is a terminal entry", Wire("terminal", killed_once)),
        gate("T9.hit-non-authority", "a hit from the fake lands once",
             line(r"enemy_net: hit slot \d+ applied hp -?\d+ -> -?\d+", "enemy_net: hit slot %u applied hp"),
             Wire("hp", hp_dropped)),
    ]


def run(ctx):
    ctx.step("Load a save with a 3-zombie room and enter it; stand back")
    if not enter_game_room(ctx, timeout=LOAD_WAIT_S):
        return
    ctx.wait_log(rf"enemy_observer: sending {EXPECTED_ENEMIES} enemies", LOAD_WAIT_S)
    ctx.wait_until(lambda: len(ctx.enemies.bound_slots()) >= EXPECTED_ENEMIES, STREAM_WAIT_S, "three enemy slots in the stream")
    ctx.step("Shoot one zombie dead")
    ctx.wait_until(lambda: any(s.terminals for s in ctx.enemies.observed.values()), ctx.duration(KILL_WAIT_S), "a terminal entry")
    if ctx.require("hit request", "T9.hit-non-authority"):
        shoot_one(ctx)
    ctx.step("Leave the room by a door")
    ctx.wait(ctx.duration(LEAVE_S))


def shoot_one(ctx):
    with ctx.lock:
        alive = [n for n, s in sorted(ctx.enemies.observed.items()) if s.alive and s.hp > 0]
        if not alive:
            ctx.skip("T9.hit-non-authority", "no living enemy in the stream")
            return
        slot = alive[0]
        ctx.enemies.arm_hp_check(slot)
        ctx.enemies.shoot(slot)
    ctx.wait_log(rf"enemy_net: hit slot {slot} applied", HIT_WAIT_S)
    ctx.wait(1.0)
