"""T1 input-mode regression: partner_mode=input, the echo peer repeats the pad and position only; no state line anywhere."""
from fake_peer import wire
from fake_peer.gates import FAIL, PASS, Result, Wire, forbid
from fake_peer.library import IN
from fake_peer.scenarios.common import gate, line

ID = "t1_input"
ROWS = "T1"
SEATS = ("guest",)
SCRATCH_SEATS = ()
MODE = "input"
SUMMARY = "partner_mode=input: echo of PLAYER_STATE / PAD_FRAME, no state-mode line or frame"
PLAY_S = 180.0
MIN_PAD_PER_S = 1.0


def pad_rate(evidence):
    pads = evidence.journal.frames(wire.Msg.PAD_FRAME, IN)
    if len(pads) < 2:
        return Result(FAIL, f"{len(pads)} PAD_FRAME received")
    seconds = (pads[-1].t - pads[0].t) / 1000.0
    rate = len(pads) / seconds if seconds else 0.0
    return Result(PASS if rate >= MIN_PAD_PER_S else FAIL, f"PAD_FRAME {rate:.0f}/s")


def no_state_types(evidence):
    state = [e for e in evidence.journal.frames() if wire.is_state_type(e.type)]
    return Result(PASS if not state else FAIL, "no state type in the journal" if not state else wire.type_name(state[0].type))


def gates(seat):
    return [
        gate("T1.config", "input mode", line(r"config: partner_mode=input", "config: partner_mode=%s")),
        gate("T1.no-state-lines", "no state-mode line",
             forbid=[forbid(r"^state_mode:", "state_mode:"), forbid(r"^state_net:", "state_net:"), forbid(r"^router:", "router:"),
                     forbid(r"^puppet:", "puppet:")]),
        gate("T1.pad", "PAD_FRAME from the game", Wire("pad rate", pad_rate)),
        gate("T1.no-state-frames", "no state frame either way", Wire("journal", no_state_types)),
    ]


def run(ctx):
    ctx.peer.status = False
    ctx.source("echo", types="input", delay=ctx.options.delay, offset_x=0.0)
    ctx.step("Set partner_mode=input in adapter.ini, start the game, load a save")
    ctx.step("Walk, fire, take a door, open the menu for 3 min")
    ctx.wait(ctx.duration(PLAY_S))
    ctx.manual("T1.visual", "Billy repeats Rebecca's pad about 1 s late")
