"""T11 boss hand-over: as the guest the fake is the new authority (Ready, claim, hold the members, Outcome on a kill, a second
round where it leaves mid-prepare); as the host it is the old authority by replay (door round, then vanish)."""
from fake_peer.gates import FAIL, MISSING, PASS, Result, Wire
from fake_peer.scenarios.common import enter_game_room, gate, line, newest_clip, run_room_by_replay

ID = "t11_boss"
ROWS = "T11 (1) (2) (3)"
SEATS = ("guest", "host")
SCRATCH_SEATS = ()
SUMMARY = "boss save: new authority seat (Ready, hold, Outcome, leave-mid) or old authority seat (door, vanish)"
LOAD_WAIT_S = 900.0
PREPARE_WAIT_S = 600.0
HAND_OVER_WAIT_S = 120.0
CANCEL_WAIT_S = 60.0
VANISH_WAIT_S = 30.0
HITS_TO_KILL = 3
PREPARE = r"boss: token \d+ prepare \(members \d+, alive [0-9a-f]+\)"
WORD = "boss: token %u %s (members %u, alive %02x)"


def members_kept_alive(evidence):
    boss = evidence.mirrors["boss"]
    if boss.new is None:
        return Result(MISSING, "no accepted prepare")
    return Result(PASS if boss.new["stage"] in ("committed", "running") else FAIL, f"stage {boss.new['stage']}")


def gates(seat):
    if seat == "guest":
        return [
            gate("T11.2", "the game offers, the fake takes the room",
                 line(PREPARE, WORD), line(r"boss: token \d+ commit", WORD), line(r"boss: token \d+ handed over", WORD),
                 Wire("fake stage", members_kept_alive)),
            gate("T11.outcome", "the fake's kill is reported", line(r"boss: token \d+ outcome", WORD, after="kill")),
            gate("T11.3", "leaving mid-prepare cancels",
                 line(r"boss: token \d+ cancelled \(left\)", "boss: token %u cancelled (%s)", after="leave-mid")),
        ]
    return [
        gate("T11.2-new", "the game as the new authority applies the hand-over",
             line(r"boss: token \d+ ready", WORD), line(r"boss: token \d+ commit", WORD),
             line(r"boss: token \d+ applied \(members \d+, alive [0-9a-f]+\)", WORD)),
        gate("T11.1", "the old authority vanishing hands the room over alone",
             line(r"room_authority: scene 0x[0-9a-f]+ owner slot -1 epoch \d+ \(peer left\)",
                  "room_authority: scene 0x%02x owner slot %d epoch %u (%s)", after="vanish"),
             line(r"enemy_observer: authority -> here", "enemy_observer: authority -> here (scene 0x%02x e%u), %u enemies resumed (%u at step 0, adopted %u)",
                  after="vanish"),
             line(r"boss: token \d+ commit alone", WORD, after="vanish")),
    ]


def run_guest(ctx):
    ctx.step("Load the boss save and enter the boss room")
    if not enter_game_room(ctx, timeout=LOAD_WAIT_S):
        return
    ctx.step("Fight the boss, then take a door out in LEAVE_BEHIND")
    ctx.wait_log(PREPARE, PREPARE_WAIT_S)
    ctx.wait_log(r"boss: token \d+ handed over", HAND_OVER_WAIT_S)
    ctx.mark("kill")
    with ctx.lock:
        ctx.boss.kill(0)
    ctx.wait(HAND_OVER_WAIT_S / 12)
    other = next((s for s in sorted(ctx.rooms.known) if s != ctx.peer.place.scene), None)
    if other is None:
        ctx.skip("T11.3", "needs a second room the game named")
        return
    ctx.step("Come back and take a door out again")
    ctx.mark("leave-mid")
    ctx.boss.leave_mid_to = other
    ctx.wait_log(r"boss: token \d+ cancelled", CANCEL_WAIT_S, since=ctx.marks["leave-mid"])


def run_host(ctx):
    newest = newest_clip(ctx.library)
    if newest is None:
        ctx.require("enemy clip", "T11.2-new", "T11.1")
        return
    (scene, _), clip = newest
    ctx.step("Start the game as the guest and load the boss save")
    ctx.wait_until(lambda: ctx.remote.pose is not None, LOAD_WAIT_S, "the game's first pose")
    run_room_by_replay(ctx, scene, clip, HITS_TO_KILL)
    ctx.step("Enter the boss room and stand")
    ctx.wait_until(lambda: ctx.remote.in_room(scene), LOAD_WAIT_S, "the game in the boss room")
    ctx.wait(VANISH_WAIT_S / 6)
    if ctx.options.round == "vanish":
        ctx.skip("T11.2-new", "round door not run: use --round door")
        ctx.mark("vanish")
        ctx.say("vanish")
        ctx.vanish()
        ctx.wait(VANISH_WAIT_S)
        return
    ctx.skip("T11.1", "round vanish not run: use --round vanish")
    with ctx.lock:
        started = ctx.boss.door()
    if not started:
        ctx.skip("T11.2-new", "the recorded room holds no boss member")
        return
    ctx.wait_log(r"boss: token \d+ applied", HAND_OVER_WAIT_S)


def run(ctx):
    (run_guest if ctx.seat == "guest" else run_host)(ctx)
