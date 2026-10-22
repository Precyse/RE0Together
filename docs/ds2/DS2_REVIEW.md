# DEATH STRANDING 2 adapter: review (2026-10-07)

Scope: `adapters/ds2` on `main` at 264c61e (about 21 000 lines; 235 source files under `src`, the largest cargo.cpp at 553 lines), reviewed system by
system for cost on the game's threads, layered workarounds and conflicting writers. The 24 commits of `ds2-live` that
are not on `main` (riding WIP, marked unverified) were not reviewed. No game run: the brief keeps the game install
read-only, and deploying `version.dll` writes into it, so every number below is from the archived live logs
(`G:\coop-scratch\ds2\session-log\`, the game's `coop\adapter.log` of 2026-10-06), offline benchmarks against `DS2.exe`
read from disk, or static reading. Live checks to run are listed per item.

Verdicts: **keep** (sound, leave it), **tidy** (small fixes, structure stays), **rebuild** (the structure itself is the
problem; a cleaner design gives the same behavior).

## Summary

| system | verdict | done in this branch | open |
|---|---|---|---|
| Networking and sync | tidy | config parser, resync file trigger | 0x012A id collision (decision), keepalive volume |
| State and snapshot pipeline | keep | one gameplay clock | |
| Combat and hit handling | tidy | puppet health no longer scans RTTI | damage log runs on every hit in shipped builds |
| Partner body and riding | rebuild (target feed, ride input) | spawn freeze removed, pool copies at spawn removed | body moved from the render thread; dead-body removal |
| Cargo | rebuild (read layer, threads) | per-frame list copies, solo pool copies | one pool read per tick; rack_sync off the net thread |
| Orders | keep | | owner scans in the slot hooks |
| Story and cutscene sync | keep | cutscene log volume | |
| World sync (clock, weather, BTs, camps, structures, focus) | keep | | clock stepping beside world_pause |
| Save, load and join | keep | | |
| Gear restore | keep | | not run live |
| Logging and diagnostics | tidy | cutscene_log, test_commands, health_watch | shared logger opens the file per line |
| Hooks layer | tidy | RTTI lookups kept and made at start-up | two simulation-thread hooks; 89 hooks enabled one by one |

Totals: keep 6, tidy 4, rebuild 2.

Top five by player impact:

1. **Body spawn froze the simulation thread 0.5 to 0.8 s** (fixed, 8a7053f). Nine RTTI scans in `silenceRemote`; the
   same scans ran on a guest for every health change of a puppet (up to 4 per change, about 60 ms each).
2. **Message id 0x012A is used twice**: `STRUCT_REQUEST` (struct_wire.h:18) and `CUTSCENE_START` (cutscene_wire.h:13).
   Works today only because the two travel in opposite directions. Wire change: needs your decision.
3. **The partner's body is moved by the overlay's draw callback** (marker_overlay.cpp:169). With `overlay=0`, or after
   the DX12 hook disables drawing on a fault, the body never moves; its smoothing runs at the render rate. Rebuild
   proposed, not done (game-visible).
4. **Every cargo query copies the whole baggage pool** (cargo.cpp:220-240, about 3 MB): up to 10 copies a second on the
   net thread with a partner and a vehicle, and `rack_sync` creates pieces and writes owner flags from the net thread,
   the pattern equip_sync was moved off for crashing. Rebuild proposed.
5. **A body that died is removed with `Entity::Remove` in a running world** (remote_player.cpp:313 into
   `forgetBody(..., true)`), the operation the HANDOFF records as crashing (0x140D93A06, twice). Park instead: needs
   your decision.

## Measurements

| what | number | source |
|---|---|---|
| One `msvc_rtti::vtableOf` lookup | about 57 ms (a pass over the 106 MB `.data` section) | `build/bench/rtti_bench.cpp` on DS2.exe |
| Body spawn, "entity is assigned" to "spawned" (only `silenceRemote` between them) | 480 to 834 ms, 29 of 29 spawns | archived logs |
| Same lookup once kept | 0.1 us (first 12 us in the test exe) | `tests/msvc_rtti_test.cpp` |
| Adapter cost on the simulation thread, idle, test config | 36 us a frame, of it `test commands` 26.9 us (3459 windows) | live `adapter.log` |
| test_commands poll: 18 open attempts vs one listing | 365 us vs 29 us per poll (live, with `coopDirectory()`'s CreateDirectory: about 1.4 ms) | `build/bench/poll_bench.cpp` |
| cutscene_log lines | 275 864 in 3 min (1 530 a second, one file open each, 141 MB log) | adapter-1006-1311/1255 logs |
| `std::mutex` + map lookup per pose evaluation | 11 ns alone, 43 ns with 4 threads, 181 ns with 8; atomic flag 0.2 ns | `build/bench/micro_bench.cpp` |
| `GetModuleHandleW(nullptr)` (every `ds2::at`) | 1.3 ns: not worth caching | micro_bench |
| Hook installation at start-up | 88 of the 89 hooks in 4.3 s, 37 to 60 ms each (each enable freezes the game's threads) | live log 14:09:05.970 to 14:09:10.321 |
| Remote body callback, live windows with a spawn | 1 146 to 1 845 us a frame averaged over 5 s (the spawn freeze) | archived logs |
| PLAYER_STATE | 24 B at 60 Hz: 1.4 KB/s | player_sync.h |
| ANIM_STATE | 2.2 to 4 KB/s (logged by anim_sync) | HANDOFF |
| VEHICLE_STATE | 64 B at 30 Hz while driving: 1.9 KB/s | vehicle_sync.h |
| ENEMY_STATE | 88 B per enemy; 10 Hz when moving, keepalive every 2 s for every tracked enemy (up to 512) | enemy_host.cpp:34,46 |

ENEMY_STATE is the one stream that grows with the world: a camp of 300 standing enemies costs 300 x 88 B / 2 s = 13 KB/s
in keepalives alone, 22 KB/s at the 512 cap, whatever the partner's distance.

## Systems

### 1. Networking and sync: tidy

What: the launcher link (`../re0/src/net_client.cpp`, shared) delivers frames to `player_sync`'s tick on the net
thread (select timeout 5 ms). `player_sync::tick` (player_sync.cpp:126-185) hands every frame to 21 module `onFrame`
functions and calls 23 module ticks; each module keeps its own interval. Clock sync, authority table and resync are
small pure modules with unit tests.

Cost: per tick a `SessionSnapshot` copy (vector of peers with names) and `rememberNames` rebuilds a map of names
(player_sync.cpp:93-105): about a microsecond, up to 200 times a second, net thread only. The fan-out costs 21
type compares per frame. Neither is worth changing.

Risk:
- **0x012A twice** (struct_wire.h:18 `kMsgStructRequest = kFirstGameType + 42`; cutscene_wire.h:13 `kMsgStart =
  kFirstGameType + 0x2A`). struct_sync.cpp:41 takes it on the host, cutscene_sync's `fromHost` on a guest; any future
  use in the other direction, or a module that forgets its role check, decodes the wrong payload. Free ids: 0x010E,
  0x0122, 0x0123, 0x012E, 0x012F, 0x0132 up.
- The resync file trigger never worked: `L"\resync_now.txt"` is a carriage return (resync_trigger.cpp:16). Fixed.
- `remote_body=yes` turned the body off while every other default-on key kept it on (config.cpp). Fixed.

### 2. State and snapshot pipeline: keep

What: host-authoritative snapshots per scope (facts, story, structures, enemies, weapons) to each new peer
(`PeerJoins`, keyed by the PEER_UP serial) and on RESYNC; the guest asks again once its own gameplay settles
(fact_sync.cpp:76 and the same in enemy, story, struct sync), because a snapshot that lands during the load is
overwritten. FACT_SNAPSHOT is chunked (256 facts) and tracked.

Cost: two snapshots per join by design (join and gameplay); small.

Risk: the guest applies facts with the engine's own writer from the net thread (fact_sync.cpp:61 into
world_facts.cpp `applyFact`). The writer locks its own stripes, but any change listener it triggers runs on the net
thread; DS2_NOTES lists change notifications on the guest as open. Moving `applyPending` to a sim_tick callback would
put it on the game's thread. Not changed (needs a live check that guest-side fact listeners behave).

Done: world_facts kept a second `GameplayClock` updated from the fact hooks and the net thread (a walk of Sam's
components every 250 ms) beside sim_tick's, which tracks the same state machine every frame. One clock now (24c69bc).

### 3. Combat and hit handling: tidy

What: one hook on `ApplyDamage` 0x1406fd570 (combat_hook.cpp:169-187) diverts hits on puppets (guest) and on the
body (host); received hits are rebuilt like script damage on the simulation thread under `remote_apply::Scope`;
`damage_veto` drops the body's own queued damage. Rules (`combat_rules.h`) and wire are unit tested.

Cost: `combat_log::before/after` (combat_hook.cpp:170-186) run on every damage in the world with no flag: a UUID read
and an `enemy_directory` lookup under its mutex per hit, contact damage arriving every few milliseconds, and a log line
(file open) per distinct tracked hit (25 248 `combat_hit` lines in the archived logs). Recommend gating it behind
`diagnostics=1` like `damage_diag`; not done because it is the team's primary combat trace.

Done: `enemy_vitals::lifeBlock` looked up up to four damage-component classes by RTTI scan for every
`applyHealth` (a guest puppet's health change, simulation thread): up to about 230 ms each. Resolved at start-up now.

### 4. Partner body and riding: rebuild (target feed and ride input)

What: a second player entity built like a net player (remote_player.cpp), silenced and guarded by about 20 hooks
(remote_camera, remote_guards, remote_context, remote_baggage, remote_marker, remote_appearance, remote_update,
player_system_guard, setdriver_guard), animated from the partner's variables before each pose evaluation
(remote_animation), placed every simulation frame or seated through the game's own ride states (remote_ride). Each
guard answers one verified crash or HUD fault (HANDOFF, DS2_NOTES "A second player entity").

Cost:
- Spawn: 0.5 to 0.8 s frozen simulation thread per spawn (every join, fast travel, load): fixed (8a7053f).
- Spawn and 15 s later: two full baggage-pool copies on the simulation thread for a diagnostic of the closed gear-pile
  hunt (remote_player.cpp, `logLooseCargo`); the HANDOFF ties pool copies on this thread to the intermittent spawn crash
  at 0x140F75E12. Removed (7df329d).
- Per frame: `remote_animation::beforePose` runs for every animated entity (remote_animation.cpp:200-214); its puppet
  branch took a global mutex per entity on the engine's pose workers even with no puppet: fixed (fe55e0c).

Risk, the reasons for the rebuild verdict:
- **The body's target is set from the render thread's draw callback** (marker_overlay.cpp:167-169 calls
  `remote_body::setTarget` with the marker's smoothed position). Body movement depends on the overlay being enabled and
  healthy, and its smoothing step is per rendered frame. Proposed: `remote_player::follow` reads
  `player_sync::remotePlayers()` itself on the simulation tick with its own blend; the marker keeps its own smoothing.
  Effect: the body moves with `overlay=0` and after an overlay fault; smoothing at the simulation rate. Verify: one PC
  with `fake_launcher.py --follow`, `overlay=0`, body follows (screenshots), and the 5 s park still triggers. Not done:
  game-visible.
- **Passenger boarding polls `GetAsyncKeyState('F')`** (remote_ride.cpp:256), not the game's own Use Vehicle action:
  it fires when F is pressed in any window and does not follow key rebinding. The HANDOFF's E1 (hook the action request
  0x140E5B3F0, kind 3) is the replacement. Not done: game-visible, unverified.
- **A dead body is removed in a running world** (remote_player.cpp:270-313: after 3 s dead, `forgetBody(..., true)`
  calls `Entity::Remove`). The HANDOFF records removal in a running world crashing twice (0x140D93A06) and the park
  path as the verified answer (20 of 20). Proposed: park the dead body and build a new one only at the next world
  build, or revive it in place. Needs your decision.
- `remote_player::leave` on a fast travel removes the entity too (`worldLeft(why, true)`); a travel tears the world
  down a moment later, which the HANDOFF considers safe, but this path has no live record in the notes.

### 5. Cargo: rebuild (read layer and threads)

What: cargo.cpp reads DSBaggageManager's pool and owners; cargo_transfer (lists, give/take, net thread),
cargo_ground (pickups and drops, net thread), rack_sync (the body's backpack mirrors the partner's, net thread),
equip_sync (the body's worn gear, simulation thread), vehicle_load (vehicle beds, net thread), partner_cargo (hooks on
the baggage slot functions), cargo_defer (deletions spread over frames).

Cost:
- `livePool` (cargo.cpp:220-240) copies the whole pool, about 3 MB, plus a vector of every live entry, for
  `carriedCargo`, `backpackCargo`, `slotPieces`, `ownedCargo`, `looseCargo` and the order-copy guard. Per second with a
  partner: cargo_transfer 2, cargo_ground 4 (`looseCargo` and `carriedCargo`, cargo_ground.cpp:106,116), rack_sync 2,
  vehicle_load 2 while a vehicle is driven: up to 10 copies (30 MB of memcpy) a second, plus one per menu event
  (`holdsOrderPiece`, partner_cargo.cpp:158). Each also walks up to 16 384 owners with two guarded reads each
  (`findOwner`, cargo.cpp:149-158) and the local player's owner tree (`sharesWithLocalPlayer`).
- equip_sync makes three head-only pool passes every 500 ms on the simulation thread (`piecesOf(0)`,
  `piecesOf(owner)`, `markOwnedCargo`), under the 20 us/frame reporting floor.
- Done: the menu and marker no longer copy both lists every render frame (f76055e); no pool copies while no partner is
  listed (abe6bf1, 8bd2e85).

Risk: rack_sync runs on the net thread and creates pieces (`addBackpackCargo`), queues deletions and writes the owner
active flag (`setOwnerActive`, cargo.cpp:403-419) on the engine's objects, the same work equip_sync was moved to the
simulation thread for (audit B1/B2, "engine reads off the net thread"). Reads off the game threads are SEH-guarded but
not consistent: an owner tree can change between `findOwner` and the slot reads.

Proposed rebuild: one pool read per simulation tick that is due (a head-only pass like `scanPool`, filling one
snapshot: backpack, body owner, loose pieces near the player, vehicle beds), published to the net thread by value;
rack_sync and vehicle_load become sim_tick callbacks fed by the net thread's received lists, as equip_sync is. Effect:
from up to 10 full copies a second on the net thread to 2 head passes on the simulation thread, and no engine writes
off the game threads. Verify: sim_tick's per-callback log shows the cost; the live rack and vehicle-bed checks of
DS2_NOTES "Partner cargo at a terminal" and "Vehicle load" pass again. Not done: touches every cargo path and needs
the live checks.

### 6. Orders: keep

What: order_gate refuses the terminal's accept callbacks on a guest (small, verified); story.cpp forwards a guest's
order start and delivery to the host; partner_cargo counts the body's rack in the carried set and hand-over gather and
reports moves at the slot functions.

Cost: `removeDetour` and `addDetour` (partner_cargo.cpp:186-210) run for every baggage slot change in the world and,
while the body exists, scan all owners once or twice (`remoteOwner`, `baggageOwner(0)`): tens of microseconds per
change, more during loads. Caching the two owner pointers per gameplay epoch would remove it; not done (pointer
lifetime across loads needs a live check).

### 7. Story and cutscene sync: keep

What: the host polls the mission map every 500 ms and hooks the request functions; a guest vetoes its own and replays
the host's (story.cpp). Cutscenes are held and released through a tested decision table (cutscene_table.h) and gate.

Cost: small. Done: cutscene_log logged every call of a pending start (1 530 lines a second while Sequences were held);
once per Sequence and decision now (b1d5096).

### 8. World sync: keep

What: clock and weather (world_env detours on the engine's updates, host every 200 ms), BT regions and catchers
(bt_events), camp alert (camp_alert, 500 ms), structures (structures.cpp, ladders), the partner as a second simulation
focus (partner_focus splits every category table each frame; sneaking_focus patches the NPC managers' distance).

Cost: partner_focus reads every member's position of every category table each frame while the body lives
(partner_focus.cpp:56-68): one guarded read per entity per frame, not in the sim_tick report because it runs inside the
engine's update. Worth a counter in the next live run.

Risk: two mechanisms keep the world going under menus: world_pause makes `IsPaused` ignore the wheel, rings and pause
menu (world_pause.cpp:44), and world_env steps the clock itself when the game's time update stops (world_env.cpp:84,
`keepClockRunning`). DS2_COOP_GAPS says the pause menu still pauses the rest of the world, so the stepping covers the
remaining case; worth confirming in one live run whether it still fires anywhere but the pause menu, and folding it
into world_pause if so.

### 9. Save, load and join: keep

What: session saves through the redirected Documents folder (documents_redirect, import patch from DllMain), the
loading screen tracked by its constructor and destructor (loading_screen), one world signal in sim_tick (`inWorld`,
`worldBuilt`, `worldReady`, `gameplaySettled`), the body taken down when the world goes (RequestChangeArea hook,
gameplay end), marked body pieces swept once per world (equip_sync), join snapshots per PEER_UP serial.

Risk: see 4 (dead-body removal). Otherwise each piece answers a verified crash and the rules are written down.

### 10. Gear restore: keep

What: `gear_restore=1` (off by default): a guest's gained gear written to `coop\personal_gear.txt` every 30 s on change
and given back at the next settled join; pure parts unit tested (gear_snapshot). Not run live (DS2_COOP_GAPS #10).

### 11. Logging and diagnostics: tidy

What: the shared logger (`../re0/src/log.cpp`) opens, appends and closes `adapter.log` for every line under one mutex,
and `coopDirectory()` (`../re0/src/paths.cpp:12`) calls GetModuleFileName and CreateDirectoryW on every call. Every log
line written from a hook therefore costs file-system calls on that game thread.

Done (b1d5096): cutscene_log volume; test_commands lists the folder once per poll (it was 75% of the adapter's
simulation-thread time while idle in the test config); health_watch registers only with `test_commands=1`.

Proposed for the shared code (joint change with the RE0 team, not done): the logger keeps the file open (or queues
lines to a writer thread) and `coopDirectory()` caches its result. Effect: a log line from a game thread becomes a
memcpy. Verify: the RE0 and DS2 logs are byte-identical in format; a burst of 10 000 lines costs under 10 ms.

Diagnostics still in the shipped path: combat_log (see 3), partner_focus's count every 5 s (2.5 lines a second in the
archived logs), main_thread's census lines.

### 12. Hooks layer: tidy

What: 89 MinHook detours at fixed addresses of Steam build 23923251, guarded by `build_guard` (PE stamp and size), a
few byte patterns, `msvc_rtti` lookups, two code patches (player_system_guard's load redirects, sneaking_focus's
distance stub), and two simulation-thread entry points.

Done (8a7053f): `msvc_rtti::vtableOf` keeps a found vtable, and remote_guards and enemy_vitals look their classes up on
the start-up thread; unit test `msvc_rtti_test`.

Open:
- **Two simulation-thread hooks.** main_thread hooks `Player::GetLastActivatedCamera` (game.cpp:90) and picks the
  busiest caller after a 3 s census (main_thread.cpp) only to run `vehicle_sync::place` (init.cpp:61-63); sim_tick hooks
  the object-list update for everything else. The live census saw one caller thread (14:09:46 "thread 42204 called the
  frame function 1008 times"). Proposed: `vehicle_sync::place` as a sim_tick callback, main_thread removed. Effect: one
  hook and one thread rule less, vehicles placed ahead of the object update like the body. Verify: one PC with
  `fake_launcher.py --drive`, the partner's truck moves smoothly and the local driver is never moved. Not done: changes
  when vehicles are placed.
- **89 hooks enabled one at a time** (`hooks::install`, shared): 4.3 s of start-up for 88 of them, a thread freeze per hook.
  MinHook's queued enable (`MH_QueueEnableHook` then one `MH_ApplyQueued`) would make it one freeze. Shared code: joint
  change.
- `remote_animation`'s variable writes read the variable array base once per variable (`writeVariable`); hoisting it
  saves a guarded read per variable per frame; below measurement noise, left.

## Documentation

- DS2_NOTES.md held three overlapping copies of its sections and a stale 135-row copy of CODEMAP's file table: merged
  to one copy, every distinct line kept once (8fd5ee2, 788 to 337 lines).
- CODEMAP: ENEMY_ANIM (0x011E) was missing; rows updated with each change. CODEMAP.md is 109 KB for 400 lines because
  the DS2 rows are paragraphs; a shorter row per file with the detail left to the file's header comment would cut what
  every agent reads first.
- Not in any module: `PendingQueue` (pending_queue.cpp, compiled into the adapter, used by no module, CODEMAP says "no
  module uses it yet") and `DelayLine` (delay_line.h, used by tests only); `AdaptiveDelay` is measured and logged but not
  applied (anim_sync.cpp:33). Keep or drop is your call; `remote_animation::applyChanges` had no caller and is removed
  (df9936f).

## Needs your decision

1. Renumber `STRUCT_REQUEST` from 0x012A to a free id (0x0132 suggested): wire change, both peers update together; the
   build check already refuses mismatched builds.
2. Move the body's target off the render thread (system 4): game-visible smoothing change.
3. A dead body: park it instead of `Entity::Remove` in a running world (system 4).
4. The cargo read layer and rack_sync on the simulation thread (system 5): the largest change here; needs the live
   rack, terminal and vehicle-bed checks.
5. ENEMY_STATE keepalives: every tracked enemy every 2 s whatever its distance. Sending keepalives only within the
   animation radius (50 m) of the partner's body, or every 10 s beyond it, keeps the wire format; far puppets then
   refresh less often (they stand still anyway; a health change is sent at once).
6. Fold main_thread into sim_tick (system 12), gate combat_log behind `diagnostics=1` (system 3), and the two shared
   changes (logger, queued hook enable) with the RE0 team.

## Live checks for this branch

No game run was possible here. Before merging, one session on the integration setup:

1. Spawn the body (`fake_launcher.py --follow`): the log's "entity is assigned" to "spawned" gap drops from about
   500 ms to a few ms; no `remote_guards` class is missing (the body is silenced as before: the bottom-left prompts do
   not flicker).
2. Guest with enemy sync: hits on puppets change their health without a sim_tick spike (`enemy puppets` stays under
   the 20 us reporting floor).
3. `sim_tick:` lines with `test_commands=1`: callbacks well under the previous 35 us a frame; `tp.txt "0 0 0"` still
   runs.
4. `cutscene_log=1` with `cutscene_share_min_frames`: one line per held Sequence and decision.
5. Create `coop\resync_now.txt`: the "Resync requested" toast and the RESYNC in fake_launcher's output.
6. A guest join: the fact, enemy and story snapshots are still asked for at gameplay (`fact_sync: gameplay started`).
7. F7 menu opens, lists both sides and moves a piece; the load overlay shows the partner's box count before the body
   exists.
