"""T7 flags: as the guest the fake sends no-op proposals and a crossing proposal for every host commit; as the host it commits
every proposal, corrects the next one (a lever pulled twice) and skips one serial (the guest must resync). In both seats it
then sends world digests built from its mirrors: equal ones (nothing may diverge), then one with an enemy flag bit flipped
(the game must report it and ask for a resync)."""
from fake_peer import wire
from fake_peer.gates import FAIL, MISSING, PASS, SOURCE_ADAPTER, Result, Wire, forbid
from fake_peer.library import IN
from fake_peer.scenarios.common import enter_game_room, gate, line

ID = "t7_flags"
ROWS = "T7 normal, F2"
SEATS = ("guest", "host")
SCRATCH_SEATS = ("host",)
SUMMARY = ("guest: no-op and crossing proposals; host: commits, a correction on the second lever pull, one skipped serial; "
           "both: equal world digests, then one with a flipped enemy flag bit")
PROPOSE_PERIOD_S = 10.0
SNAPSHOT_WAIT_S = 60.0
PLAY_S = 180.0
LOAD_WAIT_S = 600.0
STEP_WAIT_S = 300.0
RESYNC_WAIT_S = 20.0
DIGEST_EQUAL_S = 10.0
DIGEST_FLIP_WAIT_S = 30.0
REPAIR_WITHIN_S = 2.0
MS = 1000.0
DIVERGED_ENEMY_FLAGS = r"world_digest: diverged \(enemy flags\) at serial \d+"
DIVERGED_FORMAT = "world_digest: diverged (%s) at serial %u"


def empty_commits_answer_proposals(evidence):
    flags = evidence.mirrors["flags"]
    answered = [e for e in evidence.journal.frames(wire.Msg.FLAG_COMMIT, IN)
                if wire.FlagCommitHeader.unpack(e.payload[:wire.FlagCommitHeader.size]).origin == 1]
    if flags.proposals_sent == 0:
        return Result(MISSING, "no proposal sent")
    if len(answered) < flags.proposals_sent:
        return Result(FAIL, f"{len(answered)} commits of origin 1 for {flags.proposals_sent} proposals")
    return Result(PASS, f"{len(answered)} commits of origin 1 for {flags.proposals_sent} proposals")


def serials_follow(evidence):
    problems = evidence.mirrors["flags"].violations
    return Result(FAIL if problems else PASS, problems[0] if problems else "serial + 1 each time")


def resync_requested_and_answered(evidence):
    asked = evidence.journal.frames(wire.Msg.SNAPSHOT_REQUEST, IN, evidence.mark("gap", 0.0))
    answered = evidence.journal.frames(wire.Msg.JOIN_SNAPSHOT, "out", evidence.mark("gap", 0.0))
    if not asked:
        return Result(MISSING, "no SNAPSHOT_REQUEST after the gap")
    return Result(PASS if answered else FAIL, "SNAPSHOT_REQUEST answered with a JOIN_SNAPSHOT")


def digests_exchanged(evidence):
    digest = evidence.mirrors["digest"]
    if not digest.sent or not digest.received:
        return Result(MISSING, f"{digest.sent} digests sent, {digest.received} received")
    return Result(PASS, f"{digest.sent} digests sent, {digest.received} received")


def repair_asked(request):
    """The game asks for the repair (`request`) within REPAIR_WITHIN_S of reporting the flipped bit."""
    def judge(evidence):
        reports = evidence.store.search(DIVERGED_ENEMY_FLAGS, SOURCE_ADAPTER, evidence.mark("digest_flip", 0.0))
        if not reports:
            return Result(MISSING, "no divergence line")
        reported = reports[0][0].t
        asked = evidence.journal.frames(request, IN, reported, reported + REPAIR_WITHIN_S * MS)
        detail = f"{wire.type_name(request)} {'within' if asked else 'not within'} {REPAIR_WITHIN_S:.0f} s"
        return Result(PASS if asked else FAIL, detail)
    return judge


def digest_gates(seat):
    """The game is the guest when the fake is the host: a guest asks for a snapshot, a host sends RESYNC_REQUEST."""
    request = wire.Msg.SNAPSHOT_REQUEST if seat == "host" else wire.Msg.RESYNC_REQUEST
    return [
        gate("T7.digest-equal", "equal digests never diverge", Wire("digests exchanged", digests_exchanged),
             forbid=[forbid(r"world_digest: diverged", DIVERGED_FORMAT, since="digest_equal", until="digest_equal_end")]),
        gate("T7.digest-flip", "a flipped enemy flag bit is reported and a resync asked for",
             line(DIVERGED_ENEMY_FLAGS, DIVERGED_FORMAT, after="digest_flip"), Wire("repair request", repair_asked(request))),
    ]


def flag_gates(seat):
    if seat == "guest":
        return [
            gate("T7.normal", "the host's commits arrive in order",
                 line(r"flag_seq: commit \d+ \(\d+ set, \d+ cleared\) from slot 0",
                      "flag_seq: commit %u (%u set, %u cleared) from slot %d"),
                 Wire("answers", empty_commits_answer_proposals), Wire("serials", serials_follow),
                 forbid=[forbid(r"flag_seq: corrected", "flag_seq: corrected bit 0x%x to %u")]),
        ]
    return [
        gate("T7.F2-guest", "a correction is applied once",
             line(r"flag_seq: corrected bit 0x[0-9a-f]+ to [01]", "flag_seq: corrected bit 0x%x to %u", count=1)),
        gate("T7.gap", "a skipped serial asks for a resync",
             line(r"flag_seq: gap \d+ -> \d+, resync", "flag_seq: gap %u -> %u, resync"),
             Wire("resync", resync_requested_and_answered)),
    ]


def gates(seat):
    return flag_gates(seat) + digest_gates(seat)


def run_digests(ctx):
    """Both seats end with the digest steps: equal digests for DIGEST_EQUAL_S, then one flipped enemy flag bit."""
    ctx.step("Stand still while the fake sends equal world digests")
    ctx.mark("digest_equal")
    with ctx.lock:
        ctx.flags.cross = False
        ctx.digest.start()
    ctx.wait(DIGEST_EQUAL_S)
    ctx.mark("digest_equal_end")
    ctx.step("Stand still while the fake flips one enemy flag bit in its digest")
    ctx.mark("digest_flip")
    with ctx.lock:
        ctx.digest.flip()
    ctx.wait_log(r"world_digest: diverged", DIGEST_FLIP_WAIT_S, since=ctx.marks["digest_flip"])
    ctx.wait(REPAIR_WITHIN_S + 1.0)
    with ctx.lock:
        ctx.digest.stop()


def run_guest(ctx):
    ctx.step("Load a save; open a door, pick a key, pull a lever twice")
    if not enter_game_room(ctx):
        return
    ctx.flows.request_snapshot()
    ctx.wait_until(lambda: ctx.flows.echoes, SNAPSHOT_WAIT_S, "the join snapshot")
    ctx.flags.cross = True
    end = ctx.duration(PLAY_S)
    for _ in range(max(int(end // PROPOSE_PERIOD_S), 1)):
        with ctx.lock:
            ctx.flags.propose()
        ctx.wait(PROPOSE_PERIOD_S)
    run_digests(ctx)


def run_host(ctx):
    ctx.step("Start the game as the guest and load the scratch save")
    if not enter_game_room(ctx, timeout=LOAD_WAIT_S):
        return
    ctx.step("Pull the lever once")
    ctx.wait_frame(wire.Msg.FLAG_PROPOSE, timeout=STEP_WAIT_S)
    ctx.step("Pull the lever again")
    ctx.flags.correct_next = True
    ctx.wait_log(r"flag_seq: corrected", STEP_WAIT_S)
    ctx.step("Pull the lever a third time")
    ctx.flags.gap_next = True
    ctx.mark("gap")
    ctx.wait_log(r"flag_seq: gap", STEP_WAIT_S)
    ctx.wait(RESYNC_WAIT_S)
    run_digests(ctx)


def run(ctx):
    (run_guest if ctx.seat == "guest" else run_host)(ctx)
