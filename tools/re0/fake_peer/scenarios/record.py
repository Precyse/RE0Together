"""record: the fake stands beside the game and records. Nothing is asserted; the run's frames.jsonl fills the library (pose
clips between the marks you press, enemy clips, doors, hit requests, grants, floor items, boxes)."""

ID = "record"
ROWS = "library"
SEATS = ("guest",)
SCRATCH_SEATS = ()
SUMMARY = "stand source and recording only; Enter marks the start and end of a pose clip"
CLIPS = 3
LOAD_WAIT_S = 600.0


def gates(seat):
    return []


def run(ctx):
    character = "billy" if ctx.options.fake_character != "rebecca" else "rebecca"
    ctx.step("Start the game and load a save; play what should be recorded")
    ctx.wait_until(lambda: ctx.remote.pose is not None, LOAD_WAIT_S, "the game's first pose")
    ctx.source("stand")
    for number in range(1, CLIPS + 1):
        ctx.wait_enter(f"Enter starts clip {number}")
        ctx.mark_clip(character, f"clip{number}", "start")
        ctx.wait_enter(f"Enter ends clip {number}")
        ctx.mark_clip(character, f"clip{number}", "end")
