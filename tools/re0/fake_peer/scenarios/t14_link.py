"""T14 transport: the echo source under a shaped link (delay, jitter, loss), then a stall; the launchers' fresh channel and the
game's SLOW_FRAME line are read from their logs."""
import re

from fake_peer.gates import FAIL, MISSING, PASS, Result, Wire
from fake_peer.logwatch import SOURCE_HOST, SOURCE_JOIN
from fake_peer.scenarios.common import gate, line

ID = "t14_link"
ROWS = "T14"
SEATS = ("guest",)
SCRATCH_SEATS = ()
SUMMARY = "echo under --delay-ms 40 --jitter-ms 30 --loss 0.02, then a 1 s stall; fresh channel and SLOW_FRAME lines"
SHAPE = dict(latency_ms=40.0, jitter_ms=30.0, loss=0.02)
STALL_S = 1.0
PLAY_S = 120.0
LOAD_WAIT_S = 600.0
SETTLE_S = 10.0
BASE_DELAY_MS = 100.0
DELAY_SLACK_MS = 60.0
DELAY_LINE = re.compile(r"puppet: Billy motion .*?delay (-?\d+)ms")


def delay_follows_shaper(evidence):
    delays = [int(m.group(1)) for _, m in evidence.store.search(DELAY_LINE, "adapter")]
    if not delays:
        return Result(MISSING, "no puppet motion line")
    mean = sum(delays) / len(delays)
    expected = BASE_DELAY_MS + SHAPE["latency_ms"]
    return Result(PASS if abs(mean - expected) <= DELAY_SLACK_MS else FAIL, f"mean delay {mean:.0f} ms, shaper expects about {expected:.0f}")


def gates(seat):
    return [
        gate("T14.delay", "the puppet delay follows the shaper", Wire("delay", delay_follows_shaper)),
        gate("T14.fresh", "both launchers run the fresh channel",
             line(r"Fresh channel: 3 types", "Fresh channel: ", source=SOURCE_HOST),
             line(r"Fresh channel: 3 types", "Fresh channel: ", source=SOURCE_JOIN)),
        gate("T14.stall", "a stall is coalesced by the join launcher",
             line(r"Fresh channel: \d+ older frames replaced", "Fresh channel: ", source=SOURCE_JOIN,
                  after="stall")),
        gate("T14.slow-frame", "the alt-tab stall names a column",
             line(r"SLOW_FRAME \d+ms: net \d+ puppet \d+ enemies \d+ events \d+ fx \d+ other \d+", "SLOW_FRAME %ums: net %u puppet %u enemies %u events %u fx %u other %u",
                  after="alt-tab")),
    ]


def run(ctx):
    ctx.shape(**SHAPE)
    ctx.source("echo", delay=ctx.options.delay, offset_x=ctx.options.offset_x)
    ctx.step("Start the game with partner_mode=state and load a save")
    ctx.wait_log(r"state_mode: on", LOAD_WAIT_S)
    ctx.step("Walk for 2 min")
    ctx.wait(ctx.duration(PLAY_S))
    ctx.mark("stall")
    ctx.say("stall")
    ctx.shape(stall=STALL_S)
    ctx.wait(SETTLE_S)
    ctx.step("Alt-tab the game for 1 s once")
    ctx.mark("alt-tab")
    ctx.wait(SETTLE_S * 3)
