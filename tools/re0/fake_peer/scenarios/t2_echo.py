"""T2 echo test: the game plays the host, the fake guest mirrors the game's own body streams back after a delay and beside it."""
import re

from fake_peer.gates import FAIL, MISSING, PASS, Result, SumEquals, Wire, forbid
from fake_peer.scenarios.common import fx_entries_received, gate, line

ID = "t2_echo"
ROWS = "T2 a-i"
SEATS = ("guest",)
SCRATCH_SEATS = ()
SUMMARY = "echo source (delay 1.0 s, 150 units beside), doors answered, motion / FX / stale / menu checks"
PLAY_S = 120.0
MENU_S = 5.0
SILENT_S = 3.0
LOAD_WAIT_S = 600.0
RECOVER_WAIT_S = 15.0
MIN_PUPPET_LINES = 10
DELAY_MIN_MS, DELAY_MAX_MS = 60, 200
PAUSE_BIT = 0x2
PUPPET = re.compile(r"puppet: Billy motion 0x([0-9a-f]{4}) \(owner 0x([0-9a-f]{4})\) .*? mode flags ([0-9a-f]{8}) -> "
                    r"([0-9a-f]{8}) -> ([0-9a-f]{8}), .*?delay (-?\d+)ms")


def puppet_motion(evidence):
    """E1: the 1 Hz puppet lines keep the pause bit in the middle flags, restore the first, and sit near the render delay."""
    lines = [m for _, m in evidence.store.search(PUPPET, "adapter")]
    if len(lines) < MIN_PUPPET_LINES:
        return Result(MISSING, f"{len(lines)} puppet motion lines")
    for found in lines:
        before, during, after, delay = int(found.group(3), 16), int(found.group(4), 16), int(found.group(5), 16), int(found.group(6))
        if not during & PAUSE_BIT or before != after:
            return Result(FAIL, f"mode flags {before:08x} -> {during:08x} -> {after:08x}")
        if not DELAY_MIN_MS <= delay <= DELAY_MAX_MS:
            return Result(FAIL, f"delay {delay} ms")
    return Result(PASS, f"{len(lines)} lines, mode flags restored, delay in range")


def one_reset_per_load(evidence):
    resets = len(evidence.store.search(r"puppet: Billy timeline reset \(serial\)", "adapter"))
    loads = len(evidence.store.search(r"scene: load serial \d+", "adapter"))
    if not resets or not loads:
        return Result(MISSING, f"{resets} serial resets, {loads} loads")
    return Result(PASS if resets <= loads else FAIL, f"{resets} serial resets, {loads} loads")


def gates(seat):
    return [
        gate("T2a.start", "state mode on, puppet shown",
             line(r"config: partner_mode=state", "config: partner_mode=%s"),
             line(r"state_mode: on, 18 modules enabled", "state_mode: on, %zu modules enabled"),
             line(r"puppet: Billy is shown from the owner's pose", "puppet: %s is shown from the owner's pose"),
             forbid=[forbid(r"adapter: state mode", "adapter: state mode incomplete")]),
        gate("T2b.motion", "puppet motion follows the owner", Wire("puppet lines", puppet_motion),
             forbid=[forbid(r"timeline reset \(teleport\)", "puppet: %s timeline reset (%s)")]),
        gate("T2c.door", "one serial reset per room load", Wire("resets", one_reset_per_load),
             forbid=[forbid(r"timeline reset \((gap|teleport)\)", "puppet: %s timeline reset (%s)")]),
        gate("T2d.menu", "menu open keeps the puppet fresh",
             forbid=[forbid(r"puppet: Billy stale, holding", "puppet: %s stale, holding", since="menu-start", until="menu-end")]),
        gate("T2e.stale", "silence is held, then recovered",
             line(r"puppet: Billy stale, holding", "puppet: %s stale, holding", after="silent-start"),
             line(r"puppet: Billy fresh again after \d+ ms", "puppet: %s fresh again after %u ms", after="silent-start")),
        gate("T2f.weapons", "weapon effects out and replayed",
             line(r"weapon_fx: sent \d+ entries", "weapon_fx: sent %u entries"),
             line(r"weapon_fx: Billy replayed", "weapon_fx: %s replayed %s site %u"),
             SumEquals("entries", r"weapon_fx: sent (\d+) entries", fx_entries_received, "weapon_fx: sent %u entries"),
             forbid=[forbid(r"weapon_fx: Billy .* site \d+ failed", "weapon_fx: %s %s site %u failed"),
                     forbid(r"projectile of weapon", "weapon_fx: %s projectile of weapon %u")]),
        gate("T2g.body", "controller think given once",
             line(r"puppet: Billy given a controller think", "puppet: %s given a controller think"),
             forbid=[forbid(r"sub-control \d+ has no unit", "puppet: %s sub-control %u has no unit")]),
    ]


def run(ctx):
    ctx.source("echo", delay=ctx.options.delay, offset_x=ctx.options.offset_x)
    ctx.step("Start the game from Steam with partner_mode=state and load a save in a room with one zombie")
    ctx.wait_log(r"state_mode: on", LOAD_WAIT_S)
    ctx.step("2 min: walk, run, aim low mid high, fire, reload, knife, pick up an item, take a hit")
    ctx.step("Walk through a door and back")
    ctx.wait(ctx.duration(PLAY_S))
    ctx.step(f"Open the inventory for {MENU_S:.0f} s")
    ctx.mark("menu-start")
    ctx.wait(MENU_S + 2.0)
    ctx.mark("menu-end")
    ctx.say("silent")
    ctx.mark("silent-start")
    ctx.source("silent")
    ctx.wait(SILENT_S)
    ctx.source("echo", delay=ctx.options.delay, offset_x=ctx.options.offset_x)
    ctx.wait_log(r"puppet: Billy fresh again", RECOVER_WAIT_S, since=ctx.marks["silent-start"])
    ctx.step("Fire handgun, shotgun, grenade rounds, magnum, SMG, molotov, knife; tilt aim; switch weapons in the menu")
    ctx.wait(ctx.duration(PLAY_S) / 2)
    ctx.step("Cap the game at 30 fps once and watch F8")
    ctx.manual("T2d.poses", "F8 puppet poses in/s about 4 with the menu open")
    ctx.manual("T2f.fx", "F8 fx in equals fx out, fx unlisted small and steady")
    ctx.manual("T2g.torso", "Billy's torso follows the aim tilt, slide and magazine move, model changes within 1 s")
    ctx.manual("T2h.fps", "puppet poses in/s about 30 at 60 fps and at 30 fps")
    ctx.manual("T2i.wesker", "outfit_billy=a5 for 1 min: nothing odd on Billy")
