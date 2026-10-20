"""T10 push block: as the guest the fake watches the game's push, pushes the box itself along a short path, asks while the user
pushes (a deny) and lets a lease lapse; as the host it grants, takes the moves and commits."""
import re

from fake_peer import wire
from fake_peer.gates import FAIL, MISSING, PASS, Result, Wire
from fake_peer.library import IN
from fake_peer.scenarios.common import enter_game_room, gate, line

ID = "t10_props"
ROWS = "T10 host-push, fake-push, race, lapse"
SEATS = ("guest", "host")
SCRATCH_SEATS = ("host",)
SUMMARY = "push block both ways: leases, 10 Hz moves with a reliable last pose, commit, deny race, lapse"
SCENE = 0x2D
LOAD_WAIT_S = 600.0
PUSH_WAIT_S = 300.0
COMMIT_WAIT_S = 30.0
LAPSE_WAIT_S = 15.0
POSE_TOLERANCE = 1.0
MIN_MOVES = 3
COMMITTED = re.compile(r"prop: unit (\d+) committed \((-?\d+), (-?\d+), (-?\d+)\)")


def game_push_streamed(evidence):
    moves = evidence.journal.frames(wire.Msg.PROP_MOVE, IN, evidence.mark("game-push", 0.0), evidence.mark("fake-push"))
    commits = evidence.journal.frames(wire.Msg.PROP_COMMIT, IN, evidence.mark("game-push", 0.0), evidence.mark("fake-push"))
    if len(moves) < MIN_MOVES:
        return Result(MISSING, f"{len(moves)} PROP_MOVE from the game")
    if not moves[-1].flags & wire.FLAG_RELIABLE:
        return Result(FAIL, "the last PROP_MOVE is not reliable")
    return Result(PASS if len(commits) == 1 else FAIL, f"{len(moves)} moves, {len(commits)} PROP_COMMIT")


def fake_commit_matches(evidence):
    props = evidence.mirrors["props"]
    hits = evidence.store.search(COMMITTED, "adapter", evidence.mark("fake-push", 0.0))
    if not hits or not props.last_push_pos:
        return Result(MISSING, "no commit line after the fake's push")
    found = hits[0][1]
    logged = tuple(float(found.group(i)) for i in (2, 3, 4))
    close = all(abs(a - b) <= POSE_TOLERANCE for a, b in zip(logged, props.last_push_pos))
    return Result(PASS if close else FAIL, f"commit {logged} against the fake's final pose {props.last_push_pos}")


def denied_by_the_pusher(evidence):
    denied = evidence.mirrors["props"].denied
    return Result(PASS if any(holder == 0 for _, holder in denied) else MISSING, f"deny records {denied}")


def gates(seat):
    if seat == "guest":
        return [
            gate("T10.host-push", "the game's push is leased, streamed and committed once",
                 line(rf"prop: scene 0x{SCENE:02x} unit \d+ lease granted slot 0", "prop: scene 0x%02x unit %u lease %s slot %d"),
                 line(r"prop: unit \d+ committed \(", "prop: unit %u committed (%.0f, %.0f, %.0f) serial %u"),
                 Wire("moves and commit", game_push_streamed)),
            gate("T10.fake-push", "the fake's push is granted and committed at its final pose",
                 line(rf"prop: scene 0x{SCENE:02x} unit \d+ lease granted slot 1", "prop: scene 0x%02x unit %u lease %s slot %d",
                      after="fake-push"),
                 Wire("final pose", fake_commit_matches)),
            gate("T10.race", "asking during the user's push is denied naming slot 0", Wire("deny", denied_by_the_pusher)),
            gate("T10.lapse", "a silent holder's lease expires",
                 line(r"prop: scene 0x[0-9a-f]+ unit \d+ lease expired slot", "prop: scene 0x%02x unit %u lease %s slot %d", after="lapse")),
        ]
    return [
        gate("T10.guest-push", "the game's push is requested, granted and committed once",
             line(rf"prop: scene 0x{SCENE:02x} unit \d+ lease requested", "prop: scene 0x%02x unit %u lease %s slot %d"),
             line(rf"prop: scene 0x{SCENE:02x} unit \d+ lease granted", "prop: scene 0x%02x unit %u lease %s slot %d"),
             line(r"prop: unit \d+ committed \(", "prop: unit %u committed (%.0f, %.0f, %.0f) serial %u")),
    ]


def run_guest(ctx):
    ctx.step("Load a save and enter room 0x2d")
    if not enter_game_room(ctx, timeout=LOAD_WAIT_S):
        return
    ctx.step("Push the box once to its spot")
    ctx.mark("game-push")
    ctx.wait_log(r"prop: unit \d+ committed", PUSH_WAIT_S)
    unit = next(iter(ctx.props.commits), (None, None))[1]
    if unit is None or not ctx.props.box_pose(SCENE, unit):
        ctx.require("box", "T10.fake-push", "T10.lapse")
        return
    ctx.mark("fake-push")
    with ctx.lock:
        ctx.props.push(SCENE, unit)
    ctx.wait_until(lambda: ctx.props.last_push_pos, COMMIT_WAIT_S, "the fake's push to finish")
    ctx.wait_log(r"prop: unit \d+ committed", COMMIT_WAIT_S, since=ctx.marks["fake-push"])
    ctx.step("Push the box again; the fake asks at the same time")
    ctx.wait_frame(wire.Msg.PROP_MOVE, timeout=PUSH_WAIT_S)
    with ctx.lock:
        ctx.props.push(SCENE, unit)
    ctx.wait(COMMIT_WAIT_S / 3)
    ctx.mark("lapse")
    with ctx.lock:
        ctx.props.push(SCENE, unit, lapse=True)
    ctx.wait_log(r"lease expired", LAPSE_WAIT_S + 5.0, since=ctx.marks["lapse"])
    ctx.manual("T10.look", "the box moves on the game about 100 ms behind the fake's push")


def run_host(ctx):
    ctx.step("Start the game as the guest and load a scratch save")
    if not enter_game_room(ctx, timeout=LOAD_WAIT_S):
        return
    ctx.step("Push the box in room 0x2d")
    ctx.wait_log(r"prop: unit \d+ committed", PUSH_WAIT_S)


def run(ctx):
    (run_guest if ctx.seat == "guest" else run_host)(ctx)
