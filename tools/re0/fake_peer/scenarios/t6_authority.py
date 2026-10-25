"""T6 room authority: the fake follows five TEAM doors and claims every room on arrival without waiting for the game's claim (two
claims of one epoch cross), then stays behind when the game walks out alone (LEAVE_BEHIND) and claims the room it left. Last the
fake runs a recorded room by replay and leaves it with a reliable final round: the game takes the room over and adopts it."""
import re

from fake_peer.gates import FAIL, MISSING, PASS, NoneWithin, Order, Result, Wire
from fake_peer.scenarios.common import enter_game_room, fake_slot, gate, line, newest_clip, run_room_by_replay

ID = "t6_authority"
ROWS = "T6 A1, leave, adopt"
SEATS = ("guest",)
SCRATCH_SEATS = ()
SUMMARY = ("tie claims on five door arrivals; the game's owner lines equal the mirror; leave-behind release then claim; "
           "the fake simulator's final round adopted when the game takes the room")
DOORS = 5
DOORS_WAIT_S = 900.0
LEAVE_WAIT_S = 300.0
SHOW_WAIT_S = 600.0
OBSERVE_S = 5.0
ADOPT_WAIT_S = 10.0
HITS_TO_KILL = 3
SAME_ROOM_WINDOW_S = 2.0
ADOPT_LINE = re.compile(r"enemy_observer: authority -> here \(scene 0x[0-9a-f]+ e\d+\), (\d+) enemies resumed \(\d+ at step 0, adopted (\d+)\)")
OWNER_LINE = re.compile(r"room_authority: scene 0x([0-9a-f]+) owner slot (-?\d+) epoch (\d+) \((claimed|assigned|released|peer left)\)")


def lines_match_mirror(evidence):
    """After every change the game's owner line equals a record the mirror held (scene, owner, epoch)."""
    history = {(scene, owner if owner != 255 else -1, epoch) for _, scene, owner, epoch in evidence.mirrors["authority"].changes}
    hits = evidence.store.search(OWNER_LINE, "adapter")
    if not hits:
        return Result(MISSING, "no owner line")
    for logged, found in hits:
        key = (int(found.group(1), 16), int(found.group(2)), int(found.group(3)))
        if key not in history:
            return Result(FAIL, f"{logged.text} is not in the mirror")
    return Result(PASS, f"{len(hits)} owner lines agree")


def adopted_all(evidence):
    """After the fake left, the game's hand-over line reports at least one enemy and every one of them adopted from a sample."""
    hits = evidence.store.search(ADOPT_LINE, "adapter", evidence.mark("simulator-leaves", 0.0))
    if not hits:
        return Result(MISSING, "no hand-over line")
    resumed, adopted = (int(count) for count in hits[0][1].groups())
    if resumed == 0 or adopted != resumed:
        return Result(FAIL, f"{adopted} of {resumed} enemies adopted")
    return Result(PASS, f"{adopted} of {resumed} enemies adopted")


def gates(seat):
    slot = fake_slot(seat)
    return [
        gate("T6.A1-agree", "owner lines equal the mirror", Wire("mirror", lines_match_mirror)),
        gate("T6.A1-single", "no second owner within 2 s",
             NoneWithin(r"room_authority: scene (0x[0-9a-f]+) owner slot -?\d+ epoch \d+ \((?:claimed|assigned)\)", SAME_ROOM_WINDOW_S,
                        "room_authority: scene 0x%02x owner slot %d epoch %u (")),
        gate("T6.A1-stale", "the crossing claim is refused as stale",
             line(r"room_authority: scene 0x[0-9a-f]+ owner slot -?\d+ epoch \d+ \(refused ", "room_authority: scene 0x%02x owner slot %d epoch %u (refused %s)")),
        gate("T6.leave", "walking out releases the room, the fake then claims it",
             Order(r"owner slot -1 epoch \d+ \(released\)", rf"owner slot {slot} epoch \d+ \(claimed\)",
                   "room_authority: scene 0x%02x owner slot %d epoch %u (")),
        gate("T6.adopt", "the room the fake leaves is taken over from its final round",
             line(r"enemy_observer: authority -> here", "enemy_observer: authority -> here (scene 0x%02x e%u), %u enemies resumed (%u at step 0, adopted %u)",
                  after="simulator-leaves"),
             Wire("adopted equals resumed", adopted_all)),
    ]


def run(ctx):
    ctx.authority.mode = "tie"
    ctx.authority.claim_delay_s = 0.0
    ctx.step("Load a save in TEAM mode")
    if not enter_game_room(ctx):
        return
    ctx.step(f"Take {DOORS} doors into rooms with enemies")
    ctx.wait_until(lambda: len(ctx.rooms.game_doors) >= DOORS, ctx.duration(DOORS_WAIT_S), "five doors")
    ctx.step("Switch to LEAVE_BEHIND and walk out alone")
    ctx.mark("leave")
    ctx.rooms.answer = "none"
    ctx.wait(ctx.duration(LEAVE_WAIT_S))
    fake_simulator_leaves(ctx)


def fake_simulator_leaves(ctx):
    newest = newest_clip(ctx.library)
    if newest is None:
        ctx.require("enemy clip", "T6.adopt")
        return
    (scene, _), clip = newest
    ctx.step("Enter the recorded room the fake runs; it leaves after a few seconds")
    run_room_by_replay(ctx, scene, clip, HITS_TO_KILL)
    ctx.wait_log(r"enemy_observer: showing scene", SHOW_WAIT_S)
    ctx.wait(OBSERVE_S)
    ctx.mark("simulator-leaves")
    with ctx.lock:
        ctx.enemies.send_final(scene)
        ctx.enemies.stop_replay()
        ctx.authority.release_scene(scene)
    ctx.wait(ADOPT_WAIT_S)
