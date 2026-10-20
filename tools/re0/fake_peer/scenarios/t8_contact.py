"""T8 grabs and lease, the game is the authority: Billy (the fake) stands in a room, the zombie grabs him and the fake answers
each grant by a lease mode (hold, decline, expire, silent, bad-token, jump, cut), records the grant, and takes plain hits."""
import re

from fake_peer import wire
from fake_peer.gates import FAIL, MISSING, PASS, Result, Wire
from fake_peer.library import IN
from fake_peer.scenarios.common import enter_game_room, gate, line

ID = "t8_contact"
ROWS = "T8 G1 host, G1 bite, plain hits, G2, G3"
SEATS = ("guest",)
SCRATCH_SEATS = ()
SUMMARY = "lease rounds hold / decline / expire / silent / bad-token / jump / cut against the game's zombie, plain hits, latency"
ROUNDS = (
    ("hold", "Let the zombie grab Billy front and back, mash free; once shoot it mid-hold"),
    ("decline", "Let the zombie grab Billy"),
    ("expire", "Let the zombie grab Billy"),
    ("silent", "Let the zombie grab Billy"),
    ("bad-token", "Let the zombie grab Billy"),
    ("jump", "Let the zombie grab Billy"),
    ("cut", "Let the zombie grab Billy and wait"),
)
LOAD_WAIT_S = 600.0
GRANT_WAIT_S = 600.0
SETTLE_S = 6.0
PLAIN_WAIT_S = 300.0
NO_LOOP_WINDOW_MS = 1000.0
MAX_LATENCY_MS = 500.0
END_LAPSE = re.compile(r"lease: token \d+ ended \((expired|silent)\)")


def no_second_grant_after_a_lapse(evidence):
    lapses = evidence.store.search(END_LAPSE, "adapter")
    if not lapses:
        return Result(MISSING, "no lapsed lease")
    grants = evidence.journal.frames(wire.Msg.ENEMY_LEASE, IN)
    for lapse, _ in lapses:
        if any(lapse.t < g.t <= lapse.t + NO_LOOP_WINDOW_MS for g in grants):
            return Result(FAIL, f"a second grant within 1 s of: {lapse.text}")
    return Result(PASS, f"{len(lapses)} lapses, no second grant within 1 s")


def contact_to_lease_ms(evidence):
    contacts = evidence.store.search(r"contact: slot \d+ .* \(paired\)", "adapter")
    grants = evidence.journal.frames(wire.Msg.ENEMY_LEASE, IN)
    deltas = [min(g.t - c.t for g in grants if g.t >= c.t - NO_LOOP_WINDOW_MS) for c, _ in contacts
              if any(g.t >= c.t - NO_LOOP_WINDOW_MS for g in grants)]
    if not deltas:
        return Result(MISSING, "no contact line with a grant")
    worst = max(deltas)
    return Result(PASS if worst <= MAX_LATENCY_MS else FAIL, f"contact to lease up to {worst:.0f} ms")


def one_hit_per_plain_contact(evidence):
    contacts = len(evidence.store.search(r"contact: slot \d+ .* \(plain\)", "adapter"))
    hits = len(evidence.mirrors["contact"].hits)
    if not contacts:
        return Result(MISSING, "no plain contact line")
    return Result(PASS if hits == contacts else FAIL, f"{contacts} plain contacts, {hits} PLAYER_HIT")


def gates(seat):
    return [
        gate("T8.G1-host", "a grab becomes a lease",
             line(r"contact: slot \d+ \(uEnemy10\) on Billy kind 0 level 0 -> state 6 \(paired\)",
                  "contact: slot %u (%s) on %s kind %d level %d -> state %d (%s)", after="hold"),
             line(r"lease: token \d+ slot \d+ to slot 1", "lease: token %u slot %u to slot %d (%s)", after="hold"),
             line(r"lease: token \d+ ended \(released\) hp -?\d+", "lease: token %u ended (%s) hp %d", after="hold")),
        gate("T8.shot", "a shot mid-hold goes to the holder and ends the lease",
             line(r"enemy_net: hit for slot \d+ sent to slot 1", "enemy_net: hit for slot %u sent to slot %d", after="hold"),
             line(r"lease: token \d+ ended \(enemy dead\)", "lease: token %u ended (%s) hp %d", after="hold")),
        gate("T8.decline", "a declined grant ends",
             line(r"lease: token \d+ ended \(declined\)", "lease: token %u ended (%s) hp %d", after="decline")),
        gate("T8.G2", "an unanswered or silent lease lapses once, no loop",
             line(r"lease: token \d+ ended \(expired\)", "lease: token %u ended (%s) hp %d", after="expire"),
             line(r"lease: token \d+ ended \(silent\)", "lease: token %u ended (%s) hp %d", after="silent"),
             Wire("no loop", no_second_grant_after_a_lapse)),
        gate("T8.probes", "bad holder poses are refused",
             line(r"lease: pose for slot \d+ refused", "lease: pose for slot %u refused (%s)", count=2, after="bad-token")),
        gate("T8.plain", "plain hits become one PLAYER_HIT each",
             line(r"-> state \d+ \(plain\)", "contact: slot %u (%s) on %s kind %d level %d -> state %d (%s)"),
             Wire("one hit per contact", one_hit_per_plain_contact)),
        gate("T8.G3", "contact to lease latency", Wire("latency", contact_to_lease_ms)),
    ]


def run(ctx):
    ctx.step("Load a save in a room with a zombie and enter it")
    if not enter_game_room(ctx, timeout=LOAD_WAIT_S):
        return
    ctx.contact.hold_s = 6.0
    for mode, text in ROUNDS:
        grants = len(ctx.contact.grants)
        ctx.contact.lease_mode = mode
        ctx.step(text)
        ctx.mark(mode)
        ctx.wait_until(lambda: len(ctx.contact.grants) > grants, GRANT_WAIT_S, f"a grant for {mode}")
        ctx.wait(SETTLE_S)
    ctx.contact.lease_mode = "hold"
    ctx.step("Go to a bat or dog room and take a plain hit")
    ctx.wait_until(lambda: ctx.contact.hits, PLAIN_WAIT_S, "a PLAYER_HIT")
    ctx.wait(SETTLE_S)
