"""T4 join, ready and F9: the fake joins a game in progress as the guest (snapshot request, extras, ready, resync, four
refused readies) or hosts one (answers the snapshot, judges and echoes the game's ready, then a round without the echo)."""
from fake_peer.gates import FAIL, PASS, Result, Wire, forbid
from fake_peer.scenarios.common import enter_game_room, gate, line

ID = "t4_join"
ROWS = "T4 join, F9"
SEATS = ("guest", "host")
SCRATCH_SEATS = ()
SUMMARY = "snapshot, extras, SESSION_READY and its echo from either seat; guest: F9 resync and four refused readies; host: its own resync request"
STAND_S = 3.0
ECHO_WAIT_S = 30.0
STALE_WAIT_S = 3.0
LOAD_WAIT_S = 600.0
F9_WAIT_S = 120.0
NO_ECHO_OBSERVE_S = 5.0
BAD_READIES = ("unknown_snapshot", "other_room", "other_load")
REFUSAL_LINES = {"snapshot": "unknown snapshot", "room": "other room", "load": "other load", "pose": "stale pose"}


def echoes_repeat_readies(evidence):
    flows = evidence.mirrors["flows"]
    sent = set(flows.readies)
    odd = [e for e in flows.echoes if (e.scene, e.load_serial, e.snapshot_serial) not in sent]
    return Result(FAIL if odd else PASS, "an echo repeats the ready's three values")


def refused_never_echoed(evidence):
    flows = evidence.mirrors["flows"]
    extra = len(flows.echoes) - flows.echoes_before_bad
    return Result(PASS if extra == 0 else FAIL, f"{extra} echoes of the refused readies")


def gates(seat):
    if seat == "guest":
        return [
            gate("T4.host-echo", "host side of the join",
                 line(r"join_sync: snapshot sent", "join_sync: snapshot sent"),
                 line(r"join_extras: sent serial 1", "join_extras: sent serial %u"),
                 line(r"session_ready: echoed \(scene 0x[0-9a-f]+ serial 1\)", "session_ready: echoed (scene 0x%02x serial %u)")),
            gate("T4.echo-values", "the echo repeats the ready", Wire("echo values", echoes_repeat_readies)),
            gate("T4.F9", "resync gives a second pair",
                 line(r"join_extras: sent serial 2", "join_extras: sent serial %u"),
                 line(r"session_ready: echoed \(scene 0x[0-9a-f]+ serial 2\)", "session_ready: echoed (scene 0x%02x serial %u)")),
            *[gate(f"T4.refused-{kind}", f"a ready with {reason} is refused",
                   line(rf"session_ready: refused \({reason}\)", "session_ready: refused (%s) scene 0x%02x serial %u", after="bad-ready"))
              for kind, reason in REFUSAL_LINES.items()],
            gate("T4.refused-not-echoed", "no refused ready is echoed", Wire("no echo", refused_never_echoed)),
        ]
    return [
        gate("T4.guest", "guest side of the join",
             line(r"join_sync: caught up in scene", "join_sync: caught up in scene 0x%02x"),
             line(r"join_extras: applied serial 1", "join_extras: applied serial %u"),
             line(r"session_ready: sent \(scene 0x[0-9a-f]+ serial 1\)", "session_ready: sent (scene 0x%02x serial %u)"),
             line(r"session_ready: echo received", "session_ready: echo received (scene 0x%02x serial %u)")),
        gate("T4.F9", "F9 gives a second pair",
             line(r"join_extras: applied serial 2", "join_extras: applied serial %u"),
             line(r"session_ready: sent \(scene 0x[0-9a-f]+ serial 2\)", "session_ready: sent (scene 0x%02x serial %u)")),
        gate("T4.host-resync", "a RESYNC_REQUEST from the host gives a third pair",
             line(r"join_extras: applied serial 3", "join_extras: applied serial %u"),
             line(r"session_ready: sent \(scene 0x[0-9a-f]+ serial 3\)", "session_ready: sent (scene 0x%02x serial %u)")),
        gate("T4.no-echo", "without the echo the ready repeats",
             line(r"session_ready: sent \(scene 0x[0-9a-f]+ serial 4\)", "session_ready: sent (scene 0x%02x serial %u)",
                  count=2, after="no-echo"),
             forbid=[forbid(r"session_ready: echo received", "session_ready: echo received (scene 0x%02x serial %u)", after="no-echo")]),
    ]


def run_guest(ctx):
    ctx.step("Load a save and stand still; press nothing")
    if not enter_game_room(ctx):
        return
    ctx.wait(STAND_S)
    with ctx.lock:
        ctx.flows.request_snapshot()
    ctx.wait_until(lambda: ctx.flows.echoes, ECHO_WAIT_S, "the first SESSION_READY echo")
    ctx.wait(STAND_S)
    ctx.mark("resync")
    with ctx.lock:
        ctx.flows.request_snapshot()
    ctx.wait_until(lambda: len(ctx.flows.echoes) >= 2, ECHO_WAIT_S, "the second SESSION_READY echo")
    ctx.mark("bad-ready")
    ctx.flows.mark_bad_start()
    for kind in BAD_READIES:
        with ctx.lock:
            if kind == "other_room" and not any(s != ctx.peer.place.scene for s in ctx.rooms.known):
                ctx.skip("T4.refused-room", "needs a second room the game named: walk through a door first")
                continue
            ctx.flows.send_bad_ready(kind)
        ctx.wait(0.5)
    ctx.source("silent")
    ctx.wait(STALE_WAIT_S)
    with ctx.lock:
        ctx.flows.send_ready()
    ctx.wait(1.0)


def run_host(ctx):
    ctx.step("Start the game as the guest and load any save")
    if not enter_game_room(ctx, timeout=LOAD_WAIT_S):
        return
    ctx.wait_log(r"session_ready: echo received", F9_WAIT_S)
    ctx.step("Press F9 once")
    ctx.wait_log(r"join_extras: applied serial 2", F9_WAIT_S)
    ctx.say("resync")
    with ctx.lock:
        ctx.flows.send_resync_request()
    ctx.wait_log(r"join_extras: applied serial 3", F9_WAIT_S)
    ctx.flows.no_echo = True
    ctx.mark("no-echo")
    ctx.step("Press F9 again")
    ctx.wait_log(r"join_extras: applied serial 4", F9_WAIT_S)
    ctx.wait(NO_ECHO_OBSERVE_S)


def run(ctx):
    (run_guest if ctx.seat == "guest" else run_host)(ctx)
