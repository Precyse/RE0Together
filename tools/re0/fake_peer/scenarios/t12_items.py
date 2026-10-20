"""T12 items: as the guest the fake requests each floor item the moment the game's pickup motion starts (a race for the
host ledger) and asks again for a taken item; as the host it is the ledger and answers granted, claimed, taken, or not at all."""
from fake_peer import wire
from fake_peer.gates import FAIL, MISSING, PASS, Result, Wire
from fake_peer.library import IN
from fake_peer.scenarios.common import enter_game_room, gate, line

ID = "t12_items"
ROWS = "T12 I1, I4"
SEATS = ("guest", "host")
SCRATCH_SEATS = ("guest",)
SUMMARY = "guest: race every pickup, retake a taken item; host: ledger answers granted (300 ms), claimed, taken, none"
ROUNDS = 10
LOAD_WAIT_S = 600.0
DROP_WAIT_S = 120.0
RESULT_WAIT_S = 30.0
PICKUP_WAIT_S = 300.0
ANSWER_MS = 300.0
HOST_ANSWERS = (None, "claimed", "taken", "none")
ITEM_LINE = "item_ledger: take scene 0x%04x item 0x%02x committed to slot %d serial %u"


def one_floor_take_per_item(evidence):
    takes = evidence.journal.frames(wire.Msg.FLOOR_TAKE, IN)
    results = evidence.mirrors["items"].results
    if not results:
        return Result(MISSING, "no TAKE_RESULT")
    return Result(PASS if takes else FAIL, f"{len(takes)} FLOOR_TAKE from the game, {len(results)} results")


def gates(seat):
    if seat == "guest":
        return [
            gate("T12.I1", "one grant, one claim per race, one item taken",
                 line(r"item_ledger: take scene 0x[0-9a-f]+ item 0x[0-9a-f]+ committed to slot \d+ serial \d+", ITEM_LINE),
                 line(r"item_ledger: take scene 0x[0-9a-f]+ item 0x[0-9a-f]+ refused \(claimed\) for slot \d+",
                      "item_ledger: take scene 0x%04x item 0x%02x refused (%s) for slot %d"),
                 Wire("floor takes", one_floor_take_per_item)),
            gate("T12.retake", "asking again for a taken item is refused",
                 line(r"item_ledger: take scene 0x[0-9a-f]+ item 0x[0-9a-f]+ refused \(taken\)",
                      "item_ledger: take scene 0x%04x item 0x%02x refused (%s) for slot %d", after="retake")),
        ]
    return [
        gate("T12.I4", "a slow answer holds the pickup, refusals end it",
             line(r"pickup_guard: take held for the host", "pickup_guard: take held for the host"),
             line(r"pickup_guard: take refused \(claimed\)", "pickup_guard: take refused (%s)"),
             line(r"pickup_guard: take refused \(taken\)", "pickup_guard: take refused (%s)"),
             line(r"pickup_guard: take refused \(timeout\)", "pickup_guard: take refused (%s)")),
    ]


def run_guest(ctx):
    ctx.step("Load a save and enter a room with space on the floor")
    if not enter_game_room(ctx, timeout=LOAD_WAIT_S):
        return
    key = None
    for round_number in range(ROUNDS):
        ctx.step(f"Drop a herb and pick it up again ({round_number + 1} of {ROUNDS})")
        seen = len(ctx.items.floor)
        ctx.wait_frame(wire.Msg.FLOOR_PUT, timeout=DROP_WAIT_S)
        with ctx.lock:
            key = next(iter(ctx.items.known_keys(ctx.peer.place.scene)), None)
            results = len(ctx.items.results)
            if key is None or not ctx.items.race(key):
                break
        ctx.wait_until(lambda: len(ctx.items.results) > results, RESULT_WAIT_S, "a TAKE_RESULT for the race")
    if key is None:
        return
    ctx.mark("retake")
    with ctx.lock:
        ctx.items.take(key)
    ctx.wait(RESULT_WAIT_S / 6)
    ctx.manual("T12.one-herb", "one herb in total on the game after each race")


def run_host(ctx):
    ctx.step("Start the game as the guest and load a save; pick up items in three rooms")
    if not enter_game_room(ctx, timeout=LOAD_WAIT_S):
        return
    for forced in HOST_ANSWERS:
        with ctx.lock:
            ctx.items.forced = forced
            ctx.items.answer_ms = ANSWER_MS if forced is None else 0.0
        ctx.step(f"Pick up an item ({forced or 'granted after 300 ms'})")
        ctx.wait_frame(wire.Msg.TAKE_REQUEST, timeout=PICKUP_WAIT_S)
        ctx.wait(RESULT_WAIT_S / 6)
    ctx.manual("T12.no-double-prompt", "no second prompt after the held pickup")


def run(ctx):
    (run_guest if ctx.seat == "guest" else run_host)(ctx)
