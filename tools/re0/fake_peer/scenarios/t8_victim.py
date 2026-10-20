"""T8 grabs and lease, the fake is the authority: the fake runs a recorded room by replay, the game's Rebecca is the victim of
plain hits and of grabs built from recorded lease grants; the game holds the lease, the fake judges its leased poses."""
from fake_peer.gates import FAIL, MISSING, PASS, Result, Wire
from fake_peer.scenarios.common import gate, line, newest_clip, run_room_by_replay

ID = "t8_victim"
ROWS = "T8 plain hits (guest), G1 guest side"
SEATS = ("guest",)
SCRATCH_SEATS = ("guest",)
SUMMARY = "three plain hits, three recorded grabs, one expire-holder against the game's Rebecca"
LOAD_WAIT_S = 600.0
HITS = 3
GRABS = 3
HIT_GAP_S = 3.0
GRAB_WAIT_S = 120.0
RELEASE_WAIT_S = 120.0
SETTLE_S = 3.0
HITS_TO_KILL = 3


def holder_poses_accepted(evidence):
    contact = evidence.mirrors["contact"]
    if not contact.accepted_poses:
        return Result(MISSING, "no leased pose from the game")
    return Result(FAIL if contact.refused_poses else PASS,
                  f"{contact.accepted_poses} accepted, {len(contact.refused_poses)} refused")


def gates(seat):
    return [
        gate("T8.plain-guest", "plain hits are applied",
             line(r"contact: hit applied to Rebecca from local slot \d+", "contact: hit applied to %s from local slot %u", count=HITS)),
        gate("T8.G1-guest", "the game holds the grab and ends it",
             line(r"lease: token \d+ started here \(.* held\)", "lease: token %u started here (%s held)", count=GRABS),
             line(r"lease: token \d+ ended \(released\) hp -?\d+", "lease: token %u ended (%s) hp %d"),
             Wire("holder poses", holder_poses_accepted)),
        gate("T8.expire-holder", "the authority's expiry ends the hold",
             line(r"lease: token \d+ ended \(expired by the authority\)", "lease: token %u ended (%s by the authority)")),
    ]


def run(ctx):
    newest = newest_clip(ctx.library)
    ids = ("T8.plain-guest", "T8.G1-guest", "T8.expire-holder")
    if newest is None:
        ctx.require("enemy clip", *ids)
        return
    if not ctx.require("lease grant", "T8.G1-guest", "T8.expire-holder"):
        return
    (scene, _), clip = newest
    ctx.step("Load a save outside the recorded room")
    ctx.wait_until(lambda: ctx.remote.pose is not None, LOAD_WAIT_S, "the game's first pose")
    run_room_by_replay(ctx, scene, clip, HITS_TO_KILL)
    ctx.step("Enter the recorded room and stand; mash when grabbed")
    ctx.wait_until(lambda: ctx.remote.in_room(scene), LOAD_WAIT_S, "the game in the recorded room")
    for _ in range(HITS):
        with ctx.lock:
            slot = ctx.enemies.nearest_replay_slot(ctx.remote.position())
            if slot is not None:
                ctx.contact.hit(slot)
        ctx.wait(HIT_GAP_S)
    for _ in range(GRABS):
        with ctx.lock:
            slot = ctx.enemies.nearest_replay_slot(ctx.remote.position())
            started = slot is not None and ctx.contact.grab(slot)
        if not started:
            break
        ctx.wait_until(lambda: not ctx.contact.away, RELEASE_WAIT_S, "the grab to end")
        ctx.wait(SETTLE_S)
    with ctx.lock:
        slot = ctx.enemies.nearest_replay_slot(ctx.remote.position())
        if slot is not None:
            ctx.contact.grab(slot)
    ctx.wait_log(r"lease: token \d+ started here", GRAB_WAIT_S)
    with ctx.lock:
        ctx.contact.expire_holder(slot)
    ctx.wait(SETTLE_S)
