"""T5 enemy observers, the fake simulates: the fake claims a recorded room before the game enters it, replays the room's enemy
clip with a damage model, counts the game's hit requests, cuts its link for 3 s and sends the wrong-binding probes."""
from fake_peer.gates import FAIL, PASS, Result, Wire, forbid
from fake_peer.scenarios.common import fake_slot, gate, line, newest_clip, run_room_by_replay

ID = "t5_observer"
ROWS = "T5 enter, X1, unplug, X4, X2"
SEATS = ("guest", "host")
SCRATCH_SEATS = ()
SUMMARY = "the fake runs a recorded room by replay (claims first), hit requests, link cut, wrong-binding probes"
LOAD_WAIT_S = 600.0
SHOW_WAIT_S = 600.0
PROBE_DELAY_S = 10.0
HITS_WAIT_S = 180.0
CUT_S = 3.0
AFTER_CUT_S = 10.0
HITS_TO_KILL = 3


def hit_requests_counted(evidence):
    count = len(evidence.mirrors["enemies"].hit_requests)
    return Result(PASS if count >= 1 else FAIL, f"{count} hit requests from the game")


def gates(seat):
    slot = fake_slot(seat)
    return [
        gate("T5.enter", "the game shows the fake's room",
             line(rf"enemy_observer: showing scene 0x[0-9a-f]+ from slot {slot}", "enemy_observer: showing scene 0x%02x from slot %u e%u"),
             line(rf"enemy_observer: slot \d+ \(.*\) observed from slot {slot}", "enemy_observer: slot %zu (%s) observed from slot %u",
                  count=3)),
        gate("T5.X1", "no refused entry, shots reach the simulator",
             line(rf"enemy_net: hit for slot \d+ sent to slot {slot}", "enemy_net: hit for slot %u sent to slot %d"),
             Wire("hit requests", hit_requests_counted),
             forbid=[forbid(r"enemy_observer: slot \d+ entry refused", "enemy_observer: slot %u entry refused, stream", until="probe-start")]),
        gate("T5.unplug", "a silent link is held, then resumed",
             line(r"enemy_observer: slot \d+ held \(stale\)", "enemy_observer: slot %zu held (%s)", after="cut"),
             line(r"enemy_observer: slot \d+ resumed after \d+ ms", "enemy_observer: slot %zu resumed after %u ms", after="cut")),
        gate("T5.X4", "the third hit kills", line(r"enemy_observer: slot \d+ terminal", "enemy_observer: slot %zu terminal")),
        gate("T5.probes", "wrong bindings are refused",
             line(r"enemy_observer: slot \d+ entry refused, stream", "enemy_observer: slot %u entry refused, stream", after="probe-start"),
             line(rf"enemy_observer: batch from slot {slot} refused \(epoch\)", "enemy_observer: batch from slot %u refused (%s)",
                  after="probe-start")),
    ]


def run(ctx):
    newest = newest_clip(ctx.library)
    if newest is None:
        ctx.require("enemy clip", "T5.enter", "T5.X1", "T5.unplug", "T5.X4", "T5.probes")
        return
    (scene, _), clip = newest
    ctx.step("Load a save outside the recorded room")
    ctx.wait_until(lambda: ctx.remote.pose is not None, LOAD_WAIT_S, "the game's first pose")
    run_room_by_replay(ctx, scene, clip, HITS_TO_KILL)
    ctx.step("Enter the recorded room, stand back, shoot each zombie")
    ctx.wait_log(r"enemy_observer: showing scene", SHOW_WAIT_S)
    ctx.wait(PROBE_DELAY_S)
    ctx.mark("probe-start")
    with ctx.lock:
        ctx.enemies.probe("binding")
        ctx.enemies.probe("epoch")
    ctx.wait_until(lambda: len(ctx.enemies.hit_requests) >= HITS_TO_KILL, ctx.duration(HITS_WAIT_S), "three hit requests")
    ctx.mark("cut")
    ctx.shape(cut=CUT_S)
    ctx.wait(CUT_S + AFTER_CUT_S)
    ctx.manual("T5.X2", "the observer path looks the same as native, per class (wings, segments, sparks, sound)")
    ctx.manual("T5.X4-corpse", "the corpse stays on the observer's screen")
