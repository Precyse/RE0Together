"""selftest --smoke: two fakes against each other through the two real CLI launchers (no game, no port 27960).

The host fake plays the game's side of every flow (ledger, authority by replay, snapshot host), the guest fake the second
machine; each runs the same emulators. One round of every flow of the spec runs between them: session, echo, poses, door
follow (commit, busy, other room), claims converging, enemy batches and hits, PLAYER_HIT and a lease with its end, take
request and result, proposal and commit, prop lease to commit, boss prepare to outcome, PLAYER_DIED, demo skip.
"""
import shutil
import tempfile
import time
from pathlib import Path

from fake_peer import wire, wire_check
from fake_peer.launchers import Launchers, Ports, find_launcher
from fake_peer.link import GAME_PORT
from fake_peer.library import Library
from fake_peer.runner import Context, Options
from fake_peer.wire import Msg

PORTS = Ports(host_local=28961, join_local=28962, fake_bridge=28970, game_bridge=28960)
SCENE_A, SCENE_B, SCENE_C = 0x10, 0x11, 0x12
READY_S = 60.0
FLOW_S = 15.0
POLL_S = 0.05
ECHO_DELAY_S = 0.2
ECHO_OFFSET = 150.0
DOOR_MS = 100.0
HOST_STAND, GUEST_STAND = (100.0, 0.0, 0.0), (250.0, 0.0, 0.0)
BOX_UNIT = 2
ITEM_ID = 0x10
ITEM_POS = (10.0, 0.0, 0.0)
ZOMBIE_CLASS, BOSS_CLASS = 0, 14
ZOMBIE_SLOTS, BOSS_SLOT = (4, 5), 6
HIT_ATTACK_TYPE = 1
FLAG_WORD, FLAG_BIT = 3, 0x10
SHOTS_TO_KILL = 3
LEASE_HOLD_S = 1.0
SKIP_DEMO, SKIP_SERIAL = 4, 7
ENEMY_HP, BOSS_HP = 100, 500


def until(condition, what, timeout=FLOW_S):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if condition():
            return
        time.sleep(POLL_S)
    raise AssertionError(f"timed out: {what}")


def synthetic_library(vtables):
    """The recordings the flows need, as a real run would have left them (made-up scenes that only the two fakes use)."""
    library = Library()
    for source, target in ((SCENE_A, SCENE_B), (SCENE_B, SCENE_A)):
        follow = wire.DoorFollow.make(serial=1, source_scene=source, scene=target, entry=1, type=2, motion=3, flag=0)
        library.doors[(source, target)] = {"run": "smoke", "follow": follow._asdict(), "arrival": (0.0, 0.0, 0.0)}
    entries = [dict(slot=slot, class_index=ZOMBIE_CLASS, flags=wire.ENEMY_ALIVE, target=wire.REBECCA, spawn_id=7 + slot,
                    spawn_record=slot, hp=ENEMY_HP, pos=(300.0 + 40.0 * slot, 0.0, 0.0), quat=wire.IDENTITY_PACKED_QUAT,
                    record=(2, 5, 0, 0)) for slot in ZOMBIE_SLOTS]
    entries.append(dict(slot=BOSS_SLOT, class_index=BOSS_CLASS, flags=wire.ENEMY_ALIVE, target=wire.REBECCA, spawn_id=9,
                        spawn_record=3, hp=BOSS_HP, pos=(400.0, 0.0, 0.0), quat=wire.IDENTITY_PACKED_QUAT, record=(2, 5, 0, 0)))
    header = dict(sender_ms=0, seq=1, room_epoch=1, scene=SCENE_B)
    batch = wire.pack_enemy_batch(header, wire.EnemyPoseEntry, entries)
    frames = [dict(dt=50.0 * n, type=Msg.ENEMY_POSE, payload=batch) for n in range(4)]
    library.enemy_clips[(SCENE_B, 1)] = {"run": "smoke", "start": 0.0, "frames": frames}
    hit = wire.HitRequest.make(slot=0, attacker=wire.BILLY, attack_type=HIT_ATTACK_TYPE, vtable=vtables[ZOMBIE_CLASS])
    library.hit_requests[(ZOMBIE_CLASS, HIT_ATTACK_TYPE)] = {"run": "smoke", "payload": wire.HitRequest.pack(**hit._asdict())}
    grant = wire.EnemyLease.make(token=(1 << 16) | 1, hp=ENEMY_HP, record=(2, 5, 0, 0), class_index=ZOMBIE_CLASS)
    library.lease_grants[ZOMBIE_CLASS] = {"run": "smoke", "payload": wire.EnemyLease.pack(**grant._asdict())}
    library.boxes[(SCENE_B, BOX_UNIT)] = {"run": "smoke", "pos": (0.0, 0.0, 0.0), "yaw": 0.0}
    return library


def make_context(seat, bridge_port, folder, library):
    options = Options(seat=seat, transport="bridge", bridge_port=bridge_port, no_game=True, out=folder, status=True)
    ctx = Context(options)
    ctx.library = library
    ctx.start()
    return ctx


def place(ctx, scene, source="stand", **opts):
    with ctx.lock:
        ctx.rooms.known.add(scene)
        ctx.rooms.move(scene)
        ctx.sources.use(source, **opts)


# ---- flows ---------------------------------------------------------------------------------------------------------

def flow_session(a, b):
    place(a, SCENE_A, pos=HOST_STAND)
    place(b, SCENE_A, pos=GUEST_STAND)
    until(lambda: a.remote.pose and b.remote.pose, "poses both ways")
    assert a.link.session.is_host and not b.link.session.is_host, "seats follow the WELCOME"
    b.flows.request_snapshot()
    until(lambda: b.flows.echoes, "the SESSION_READY echo")
    echo = b.flows.echoes[0]
    assert (echo.scene, echo.snapshot_serial) == (SCENE_A, 1), f"echo {echo}"
    assert not a.flows.refusals, f"refusals {a.flows.refusals}"


def flow_ready_refusals(a, b):
    b.flows.mark_bad_start()
    for kind in ("unknown_snapshot", "other_load"):
        b.flows.send_bad_ready(kind)
    until(lambda: len(a.flows.refusals) >= 2, "two refused readies")
    assert {r[0] for r in a.flows.refusals} == {"unknown snapshot", "other load"}, a.flows.refusals
    assert len(b.flows.echoes) == b.flows.echoes_before_bad, "a refused ready was echoed"


def flow_echo(a, b):
    a.sources.use("echo", delay=ECHO_DELAY_S, offset_x=ECHO_OFFSET)
    until(lambda: b.remote.pose and abs(b.remote.pose.pos[0] - (GUEST_STAND[0] + ECHO_OFFSET)) < 1.0, "the echoed pose beside the sender")
    assert a.sources.echoed > 0
    a.sources.use("stand", pos=HOST_STAND)
    until(lambda: abs(b.remote.pose.pos[0] - HOST_STAND[0]) < 1.0, "the host's own pose again")


def flow_claims_converge(a, b):
    """Two claims of one epoch from two slots cross on the wire; both mirrors settle on the lower slot."""
    a.authority.mode = b.authority.mode = "first"  # no reconcile: neither fake stands in the room it claims
    with a.lock:
        a.authority.claim_scene(SCENE_C)
    with b.lock:
        b.authority.claim_scene(SCENE_C)
    until(lambda: a.authority.table.owner_of(SCENE_C) == b.authority.table.owner_of(SCENE_C) == 0, "the lower slot owns the room")
    assert a.authority.table.epoch_of(SCENE_C) == b.authority.table.epoch_of(SCENE_C) == 1


def flow_doors(a, b):
    a.authority.mode = "first"
    with a.lock:
        a.authority.claim_scene(SCENE_B)
    b.rooms.door_ms = DOOR_MS
    assert a.rooms.run_door(SCENE_A)
    until(lambda: a.peer.place.scene == SCENE_B == b.peer.place.scene, "both in the door's target")
    assert a.rooms.results == {1: "commit"} and b.rooms.answered == [(1, "commit")], (a.rooms.results, b.rooms.answered)
    b.rooms.answer = "busy"
    assert a.rooms.run_door(SCENE_B)
    until(lambda: a.peer.place.scene == SCENE_A, "the busy door moves the runner at once")
    assert a.rooms.results[2] == "busy"
    b.rooms.answer = "commit"
    assert a.rooms.run_door(SCENE_A)
    until(lambda: a.peer.place.scene == SCENE_B, "a door from another room")
    assert a.rooms.results[3] == "other room"


def flow_enemies(a, b):
    a.sources.use("stand", pos=HOST_STAND)
    assert a.enemies.start_replay(SCENE_B, hits_to_kill=SHOTS_TO_KILL)
    until(lambda: len(b.enemies.bound_slots()) == 3, "three enemy slots observed")
    for shot in range(SHOTS_TO_KILL):
        assert b.enemies.shoot(ZOMBIE_SLOTS[0]), "a captured hit request exists"
        until(lambda: len(a.enemies.hit_requests) > shot, f"hit request {shot + 1}")
    until(lambda: b.enemies.observed[ZOMBIE_SLOTS[0]].terminals >= 1, "the kill as a terminal entry")
    assert not b.enemies.violations, b.enemies.violations


def flow_contact(a, b):
    b.contact.hold_s = LEASE_HOLD_S
    slot = ZOMBIE_SLOTS[1]
    hp = b.peer.hp
    assert a.contact.hit(slot)
    until(lambda: len(b.contact.hits) == 1, "the plain hit")
    assert b.peer.hp < hp and not b.contact.refused_hits, b.contact.refused_hits
    assert a.contact.grab(slot)
    until(lambda: not a.contact.away and not b.contact.held and a.contact.ends, "the lease to end")
    assert a.contact.accepted_poses > 0 and not a.contact.refused_poses, (a.contact.accepted_poses, a.contact.refused_poses)
    assert a.contact.ends[-1][0].reason == wire.END_RELEASED, a.contact.ends


def flow_items(a, b):
    key = (SCENE_B, ITEM_ID, ITEM_POS)
    b.items.learn(*key)
    assert b.items.take(key)
    until(lambda: len(b.items.results) == 1, "the grant")
    assert b.items.results[0][1] == wire.ANSWER_GRANTED
    until(lambda: a.items.ledger.consumed_keys(), "the item consumed in the ledger")
    assert b.items.take(key)
    until(lambda: len(b.items.results) == 2, "the refusal")
    assert b.items.results[1][1] == wire.ANSWER_TAKEN


def flow_flags(a, b):
    b.flags.propose()
    until(lambda: not b.flags.proposed and b.flags.serial == 1 == a.flags.serial, "the empty commit answering a no-op proposal")
    b.flags.cross = True
    a.flags.commit_own([dict(word=FLAG_WORD, set=FLAG_BIT, clear=0)])
    until(lambda: b.flags.words[FLAG_WORD] & FLAG_BIT and a.flags.serial == b.flags.serial == 3, "the commit and the crossing proposal")
    assert not b.flags.violations, b.flags.violations


def flow_props(a, b):
    assert b.props.push(SCENE_B, BOX_UNIT)
    until(lambda: b.props.last_push_pos and (SCENE_B, BOX_UNIT) in a.props.commits, "the push to commit on the host")
    commit = a.props.commits[(SCENE_B, BOX_UNIT)]
    assert commit.committer == 1 and all(abs(x - y) < 1.0 for x, y in zip(commit.pos, b.props.last_push_pos)), commit
    until(lambda: (SCENE_B, BOX_UNIT) in b.props.commits, "the commit on the pusher")
    assert not a.props.violations, a.props.violations


def flow_boss(a, b):
    a.authority.mode = "normal"
    assert a.boss.door(leave_to=SCENE_A)
    until(lambda: b.boss.new and b.boss.new["stage"] == "running", "the new authority to take the room")
    assert b.boss.kill(0)
    until(lambda: any(phase == wire.BOSS_OUTCOME for phase, _, _ in a.boss.log), "the outcome")
    assert [p for p, _, _ in a.boss.log] == [wire.BOSS_READY, wire.BOSS_OUTCOME], a.boss.log


def flow_story(a, b):
    a.story.die()
    until(lambda: b.remote.died == [a.peer.character], "PLAYER_DIED")
    b.story.request_skip(SKIP_DEMO, SKIP_SERIAL)
    until(lambda: a.story.skips and a.story.skips[0][0] == "DEMO_SKIP_REQUEST", "DEMO_SKIP_REQUEST")
    a.story.send_skip(SKIP_DEMO, SKIP_SERIAL)
    until(lambda: any(name == "DEMO_SKIP" for name, _ in b.story.skips), "DEMO_SKIP")


FLOWS = (flow_session, flow_ready_refusals, flow_echo, flow_claims_converge, flow_doors, flow_enemies, flow_contact, flow_items,
         flow_flags, flow_props, flow_boss, flow_story)


def check_launcher_logs(launchers):
    problems = []
    for role, path in launchers.logs.items():
        text = path.read_text(encoding="utf-8", errors="replace")
        if "Fresh channel: 3 types" not in text:
            problems.append(f"{role} launcher printed no 'Fresh channel: 3 types'")
        if "Peer budget:" in text:
            problems.append(f"{role} launcher dropped frames over its peer budget")
    return problems


def run():
    """Returns the list of problems (empty = every flow completed)."""
    try:
        launcher = find_launcher()
    except FileNotFoundError as error:
        return [f"smoke needs a launcher build: {error}"]
    folder = tempfile.mkdtemp(prefix="fake_peer_smoke_")
    sandbox = Path(folder) / "sandbox"
    sandbox.mkdir()
    launchers = Launchers(launcher, folder, PORTS, fake_seat="guest", game_dir=sandbox, save_source=sandbox)
    contexts, problems = [], []
    try:
        launchers.start()
        library = synthetic_library(wire_check.vtables("kEnemyVtables"))
        a = make_context("host", PORTS.game_bridge, folder, library)
        contexts.append(a)
        b = make_context("guest", PORTS.fake_bridge, folder, library)
        contexts.append(b)
        for ctx in contexts:
            until(lambda ctx=ctx: ctx.link.session.ready, "the session to form", READY_S)
            assert ctx.link.port != GAME_PORT, "the game's own port stays untouched"
        for flow in FLOWS:
            try:
                flow(a, b)
                print(f"smoke {flow.__name__} ok")
            except (AssertionError, KeyError, TypeError, AttributeError) as error:
                problems.append(f"smoke {flow.__name__}: {type(error).__name__}: {error}")
        for ctx in contexts:
            if ctx.dispatcher.malformed or ctx.dispatcher.faults:
                problems.append(f"malformed {dict(ctx.dispatcher.malformed)} faults {dict(ctx.dispatcher.faults)}")
    finally:
        for ctx in contexts:
            ctx.stop()
        launchers.stop()
    problems += check_launcher_logs(launchers)
    if not problems:
        shutil.rmtree(folder, ignore_errors=True)
    else:
        problems.append(f"smoke folder kept: {folder}")
    return problems
