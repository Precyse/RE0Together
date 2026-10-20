"""T13 death, save gate and cutscene skip: the fake stands in a door and reports a busy think state while the user saves, asks
the game to skip a cutscene it leads, and dies once; the game's own death is checked from its PLAYER_DIED."""
from fake_peer.gates import MISSING, PASS, Result, Wire
from fake_peer.scenarios.common import enter_game_room, gate, line

ID = "t13_death_save"
ROWS = "T13 S save gate, S3 skip, D1"
SEATS = ("guest",)
SCRATCH_SEATS = ("guest",)
SUMMARY = "save gate (door, busy think), cutscene skip request, the fake's death and the game's own"
LOAD_WAIT_S = 600.0
SKIP_WAIT_S = 600.0
DEATH_WAIT_S = 300.0
HOLD_S = 6.0
SETTLE_S = 5.0
DEMO_LED = r"demo_skip: demo (\d+) of serial (\d+) led here"


def game_skip_received(evidence):
    skips = [name for name, _ in evidence.mirrors["story"].skips if name == "DEMO_SKIP"]
    return Result(PASS if skips else MISSING, f"{len(skips)} DEMO_SKIP from the game")


def game_death_reported(evidence):
    died = evidence.mirrors["remote"].died
    return Result(PASS if died else MISSING, f"PLAYER_DIED from the game: {died}")


def gates(seat):
    return [
        gate("T13.S-door", "a save waits while the partner is in a door",
             line(r"session_slot: save waits for plain gameplay \(partner is in a door\)",
                  "session_slot: save waits for plain gameplay (%s)", after="door")),
        gate("T13.S-busy", "a save waits while the partner's think is busy",
             line(r"session_slot: save waits for plain gameplay \(partner is in an action\)",
                  "session_slot: save waits for plain gameplay (%s)", after="busy")),
        gate("T13.S3", "the fake's skip request is led and answered",
             line(DEMO_LED, "demo_skip: demo %u of serial %u %s"),
             line(r"demo_skip: skip \d+ sent", "demo_skip: %s %u sent"), Wire("DEMO_SKIP", game_skip_received)),
        gate("T13.D1", "the fake's death ends the game",
             line(r"player_damage: remote character 0 died", "player_damage: remote character %u died"),
             line(r"player_damage: game over requested for the dead partner", "player_damage: game over requested for the dead partner"),
             line(r"phase: \w+ -> Dead", "phase: %s -> %s")),
        gate("T13.D1-own", "the game's own death is reported", Wire("PLAYER_DIED", game_death_reported)),
    ]


def run(ctx):
    ctx.step("Load a save and stand still")
    if not enter_game_room(ctx, timeout=LOAD_WAIT_S):
        return
    save_gate(ctx)
    cutscene_skip(ctx)
    ctx.step("Wait for the fake to die")
    ctx.mark("death")
    with ctx.lock:
        ctx.story.die()
    ctx.wait_log(r"phase: \w+ -> Dead", DEATH_WAIT_S, since=ctx.marks["death"])
    ctx.step("Continue, then die once yourself")
    ctx.wait_until(lambda: ctx.remote.died, DEATH_WAIT_S, "the game's PLAYER_DIED")


def save_gate(ctx):
    other = next((s for s in sorted(ctx.rooms.known | ctx.rooms.library_scenes()) if s != ctx.peer.place.scene), None)
    if other is None:
        ctx.skip("T13.S-door", "needs a second room the game named: walk through a door first")
    else:
        ctx.step("Save at a typewriter now")
        ctx.mark("door")
        with ctx.lock:
            ctx.story.in_door(other)
        ctx.wait(HOLD_S)
        with ctx.lock:
            ctx.story.clear_door()
        ctx.wait(SETTLE_S)
    ctx.step("Save at a typewriter again")
    ctx.mark("busy")
    ctx.story.busy_think()
    ctx.wait(HOLD_S)
    ctx.story.busy_think(False)
    ctx.wait(SETTLE_S)
    ctx.manual("T13.S-saved", "the save went through after the fake cleared the door and the busy think")


def cutscene_skip(ctx):
    ctx.step("Play a cutscene and do not skip it")
    hit = ctx.wait_log(DEMO_LED, SKIP_WAIT_S)
    if hit is None:
        ctx.skip("T13.S3", "no cutscene led by the game")
        return
    with ctx.lock:
        ctx.story.request_skip(int(hit[1].group(1)), int(hit[1].group(2)))
    ctx.wait(SETTLE_S)
