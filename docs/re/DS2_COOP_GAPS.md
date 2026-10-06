# DS2 co-op gaps (audit 2026-10-06)

What a real two-player session hits that the single-player game assumes away. Each candidate was checked against `main` (867c42c); work only on `ds2-live` or `ds2-integration` is named where it matters. Status: **done** (code exists, may be unverified live), **partial**, **missing**. Owners: ds2-live (partner body lifecycle, vehicles), ds2-tester (combat, tracer), ds2-orders (orders, cargo), ds2-story (story, cutscenes), ds2-streaming (world focus, weather, time, structures), or new.

## Full candidate list

### Session flow
| candidate | status | owner | file | note |
|---|---|---|---|---|
| Host, join, save sync at join | done | launcher | `SaveSyncCoordinator.cs`, `games/ds2.json`, `documents_redirect.cpp` | |
| Join catch-up: facts, story, structures, enemies | done | ds2-streaming / ds2-story | `fact_sync.cpp`, `story_sync.cpp`, `struct_sync.cpp`, `enemy_sync.cpp` | asked for at the guest's gameplay |
| Host's later saves reach the guest | done | new | `launcher/SaveWatcher.cs`, `SaveSyncCoordinator.cs`, `SaveReceiver.cs` (CI-compiled only; logic checked with a scratch harness) | a guest that reloads or restarts after a crash loads the join-time world; snapshots patch facts, story and structures but not cargo, vehicles or ground pieces |
| Guest's own progress (gear, equipment, levels) kept after the session | partial (archived, not merged) | new | `launcher/AdapterSettings.cs` | the session folder is deleted at launcher exit; roadmap end goal 6 |
| Partner leaves: body | partial | ds2-live | `ds2/remote_player.cpp` | removing the entity in a running world crashes, so the body stands frozen where the link dropped; ds2-live has an unverified 5 s removal |
| Partner rejoins: one body, gear and weapon rebuilt | unknown | ds2-live | `ds2/remote_player.cpp`, `equip_sync.cpp` | verify a second body is not built beside the frozen one |
| Role switch (guest becomes host) | partial | ds2-tester | `ds2/enemy_puppet.cpp` (`releaseToHost`) | built, not live |
| Game build mismatch (Steam updates DS2.exe) | missing | new | `launcher/SteamLibrary.cs`, `adapters/ds2/src/init.cpp` | most hooks are fixed VAs of v1.10.89.0; nothing checks the build |

### Gameplay sync
| candidate | status | owner | file |
|---|---|---|---|
| Local Sam as passenger when the partner drives ("Use Vehicle" prompt red) | missing | ds2-live | `ds2/remote_ride.cpp`, `vehicle_sync.cpp` |
| Spawn crash 0x140F75E12 (DSPlayerSystem singleton writes) | partial | ds2-live | `ds2/remote_player.cpp` (guard on ds2-live) |
| Enemies and weapons in a normal session | partial | ds2-tester | `config.h`: `enemy_sync` and `weapon_sync` default off |
| BT grab of the guest's body | missing | ds2-streaming | `ds2/bt_events.h` TODO |
| Guest builds structures and roads | built, not run live (STRUCT_REQUEST, guest collapse; roads: plan in DS2_NOTES, needs live observation) | ds2-streaming | `ds2/structures.cpp` refuses the guest's placements; ladders only |
| Guest voidout or death crater | missing | ds2-streaming | none; a world change made only in the dying player's world |
| Partner in another region or map | partial (a "too far" line past 700 m; options in "Far partner" below) | ds2-streaming | `ds2/remote_player.cpp` (body leaves on area change), `marker_overlay.cpp` (distance only) |
| Guest-initiated hand-over | missing | ds2-orders | `cargo_menu.cpp` is the host's (F7) |
| Guest orders and deliveries | missing (by design for now) | ds2-orders | `ds2/order_gate.cpp` |
| Shared locker | missing | ds2-orders | none |
| Cutscenes for the partner | partial | ds2-story | `cutscene_log.cpp`; cutscene sync on `ds2-integration` |

### Player-facing QoL
| candidate | status | owner | file | note |
|---|---|---|---|---|
| Partner name and distance | done | | `marker_overlay.cpp` | |
| Partner off screen (behind the camera) | done | new | `marker_overlay.cpp` | edge arrow with name, distance, health and state; unverified live |
| Partner health, down, dead, loading, in a menu | missing | new | `player_sync.cpp` | PLAYER_STATE has a reserved u32 that could carry these |
| Ping or marker | missing | new | none | the game has its own signs; a co-op ping at the aim point is a later item |
| Partner-left notice | done | | `player_sync.cpp` | toast with name |
| Warp to the partner | done | | `ds2/warp.cpp` (F6) | |
| Pause behaviour | partial | ds2-streaming | `ds2/world_pause.cpp` | the world runs under wheels and rings; under the pause menu the clock is kept but the rest pauses on that machine (TESTPLAN row 17) |
| Controls overlay | partial | new | `adapters/ds2/README.md` | the user's rule is no explainer text in UI; the README is stale (says `remote_body=0`, misses F6) |

### Failure handling
| candidate | status | owner | file |
|---|---|---|---|
| One player dies or respawns | partial | ds2-live | checked once on one PC; the partner is not told |
| Desync recovery | done | new | `resync_trigger.cpp` (F9 and file) |
| Game crash on one side | partial | launcher | dumps reach the host (`LogForwarder.cs`); no relaunch path |

### Setup QoL
| candidate | status | owner | file |
|---|---|---|---|
| Frame generation must be off for the overlay | missing | new | `dx12_hook.cpp`; only in the roadmap checklist |
| Shipped `adapter.ini` defaults | partial | ds2-tester | `config.h` |
| Logs to send | partial | launcher | see the launcher section in `RE0_COOP_GAPS.md` |

## Top 10 (missing or partial), ranked by player impact over effort

### 1. Installed game build is never checked (S, new; adapter side done in `build_guard.cpp`, launcher buildid check is launcher-app's)
- **Root change:** the launcher checks the appmanifest `buildid` against `supportedBuilds` in `games/ds2.json` and sends it in BUILD_INFO; the adapter also compares DS2.exe's PE TimeDateStamp with the one its addresses were taken from and, on a mismatch, installs no hooks and shows one toast. DS2 updates often and every fixed VA breaks at once.
- **Files:** `launcher/SteamLibrary.cs`, `GameProfile.cs`, `BuildCheck.cs`, `games/ds2.json`, `adapters/ds2/src/init.cpp`, `ds2/game.cpp`.
- **Verify:** `tools/build_check_test.py` with a game build field; a unit test of the stamp compare on a copied header.

### 2. The partner's body freezes in place when they leave (M, ds2-live)
- **Root change:** park the body instead of removing it: hide its model and move it far below the world (or out of streaming) when the peer has been gone 5 s, and reuse the same body when a peer comes back; never `Entity::Remove` in a running world.
- **Files:** `adapters/ds2/src/ds2/remote_player.cpp`, `ds2/body.cpp`.
- **Verify:** `tools/ds2/fake_launcher.py --follow`, kill it, body hidden within 5 s; restart it, the same body returns (one `remote_body: spawned` line in total).

### 3. Partner status: off screen, health, down (S/M, new; built, see DS2_NOTES "Partner status")
- **Root change:** PLAYER_STATE's reserved u32 carries flags (dead, down, loading, in menu, driving) and a health byte; the marker shows health and state, and becomes an arrow at the screen edge when the partner is behind the camera or off screen; a toast when the partner dies or respawns.
- **Files:** `adapters/ds2/src/player_sync.cpp`, `marker_overlay.cpp`, `world_to_screen.h` (edge clamp, unit tested), `ds2/player_state.cpp` (health read).
- **Verify:** `tests/world_to_screen_test.cpp` for the edge clamp; `fake_launcher.py --at X,Y,Z` behind Sam; a fake flag word from fake_launcher.

### 4. Enemies and weapons are off in a shipped session (S, ds2-tester)
- **Root change:** once the two-PC combat rows pass, flip `enemy_sync` and `weapon_sync` defaults on; until then the README says what is off. Fix the stale README (`remote_body` default, F6 warp, F7 menu).
- **Files:** `adapters/ds2/src/config.h`, `config.cpp`, `adapters/ds2/README.md`, `tools/publish_check.py` (keep rejecting test flags).
- **Verify:** fresh `coop\adapter.ini` written by the first start shows the new defaults; publish_check passes.

### 5. The host's later saves never reach the guest (M, new; built: watcher on the host, staging on the guest, promoted while DS2.exe is not running)
- **Root change:** the host launcher watches `hostSaveDir` for written `*.dat` (game-agnostic, FileSystemWatcher with a settle delay) and re-sends through the existing FILE_* path; the guest stores them in a staging folder and moves them into the session folder only while its game is not running or is on the title screen, so the guest's own autosaves are never overwritten mid-play.
- **Files:** `launcher/SaveSyncCoordinator.cs`, `SaveSender.cs`, `SaveReceiver.cs`, `docs/CONTRACT.md`.
- **Verify:** `tools/ds2/save_sync_test.py` with a file touched on the host side after the join: the guest's staging folder gets it and the session folder only after the fake game exits.

### 6. Resync on a key (S, new; built: F9 is polled like F6/F7, not claimed, so the game still sees it)
- **Root change:** a key (F9) claimed through `input_filter` does what `resync_now.txt` does: asks every peer for all scopes and resends its own; a toast confirms it.
- **Files:** `adapters/ds2/src/resync_trigger.cpp`, `input_filter.cpp`.
- **Verify:** `fake_launcher.py` prints the RESYNC it receives; `tests/resync_test.cpp` unchanged.

### 7. Local Sam cannot ride as passenger when the partner drives (L, ds2-live)
- **Root change:** roadmap item 4 and audit E1: hook the action request 0x140E5B3F0 (kind 3 = enter vehicle) to see why the prompt is red on a vehicle the partner drives, then seat Sam with the passenger request and keep the local vehicle copy kinematic under him.
- **Files:** `adapters/ds2/src/ds2/remote_ride.cpp`, `vehicle_sync.cpp`, `ds2/setdriver_guard.cpp`.
- **Verify:** `fake_launcher.py --drive ID --drive-role 0` drives a loop with Sam pressing F at the door: Sam seated, camera follows, exits beside the vehicle.

### 8. The world stops under one player's pause menu (M, ds2-streaming)
- **Root change:** extend `world_pause` to the pause menu's own state so only the menu's player is paused, the rest of the world keeps running (TESTPLAN row 17, DS2_NOTES "World clock under menus").
- **Files:** `adapters/ds2/src/ds2/world_pause.cpp`.
- **Verify:** one PC with `fake_launcher.py --follow`: open the pause menu, the partner body keeps walking and the clock advances (two screenshots 5 s apart).

### 9. The guest cannot hand cargo over (M, ds2-orders)
- **Root change:** the same F7 menu on the guest sends a give request to the host (the host stays the decider, as with CARGO_TAKE), so either player can start a hand-over.
- **Files:** `adapters/ds2/src/cargo_menu.cpp`, `cargo_transfer.cpp`.
- **Verify:** `tools/ds2/fake_peer.py --host --pickup-answers` style host answering a guest give; the piece leaves the game's backpack and is reported added.

### 10. The guest's own progress is lost at session end (L, new; first step done: `AdapterSettings.Reset` archives the session saves; the merge is open)
- **Root change:** the save files hold world and personal state together, and every join overwrites the guest's copy with the host's, so the real fix is a merge: find which save blocks are personal (equipment, levels, private locker) and carry them from the guest's last session save into the next session's copy. First step (S): archive the guest's session save at session end instead of deleting it, so nothing is lost while the merge is worked out.
- **Files:** `launcher/AdapterSettings.cs`, `SaveSyncCoordinator.cs`, `SavePaths.cs`, `docs/CONTRACT.md`, `docs/DS2_NOTES.md` (save layout).
- **Merge plan (not built: the repo holds no DS2 save-format notes, and the saves are the user's real ones, never to be read or written by tests):**
  1. (Done, see DS2_NOTES "Save container": the files are encrypted with a per-file u32 seed in a 32-byte header; the cipher is found and `tools/ds2/savefmt.py` decrypts copies; next is naming which of the 66 chunks hold equipment and levels: diff the decrypted chunks of two gear-only saves.) Static: from the archived guest saves (`coop/archive/<stamp>/`, a copy, never the user's folder) check whether `autosave*.dat` / `profile.dat` are compressed or encrypted, and find the container's block table (names or type ids per block).
  2. Diff two archived guest saves that differ only in gear (equip a different item, save twice, in a scratch copy of the game folder) to name the personal blocks (equipment, levels, private locker); the world blocks stay the host's.
  3. Merge on the guest launcher in `SaveReceiver` after a file verifies: copy only the personal blocks from the newest archive's same-named save into the received host save, fix up any checksum or length, and keep the unmerged host file next to it as `<name>.host`. Refuse (use the host file untouched) whenever a block is missing, a length or checksum does not fit, or the save version differs.
  4. Verify: `tools/ds2/save_sync_test.py` with a synthetic save container built from the real block table; then one live join on the scratch copy.
  Risk: a wrong block corrupts the guest's save at join, so nothing ships before steps 1 and 2 give a block table.
- **Verify:** `tools/ds2/save_sync_test.py`: end a session, the guest's session files are in the archive folder; the merge itself needs a save diff of two guest saves that differ only in gear.

## Far partner: beyond the host's loaded world (ds2-streaming, 2026-10-06)

Measured live: the engine streams tiles and simulates entities around ONE point, this machine's player (streaming manager observer, +0x20; no second observer exists, the only override is the cutscene export `GameModule SetObserverPositionOverride`). The entity tables' radii are 500 to 1000 m (table 3: 768 / 800). `partner_focus` and `sneaking_focus` make the partner count as a focus for the activity decisions, but a member only wakes when its tile data is streamed in (stream test 2): with the partner 3 km from the host's player every enemy-type member near it stayed asleep (stream test 0), 525 m away they woke (3 of 4 at a camp).

| option | what it gives | cost / risk |
|---|---|---|
| A. Soft leash notice (built) | the player sees "name  N m  too far" past 700 m, so a missing fight is explained | no gameplay change; the world around a far partner is still not simulated |
| B. Hard leash | warp the partner back (F6 exists) or block fast travel / vehicles past a limit | removes the problem by removing freedom; feels restrictive |
| C. Observer at the midpoint (tried, removed) | feed the engine's own observer override (SetObserverPositionOverride 0x14071fdb0) with a point half way to the partner, capped at 300 m from the host's player | live at 965 m: the observer moved, the far camp did not stream in within 2.7 min (not proven); the code was removed |
| D. Each machine simulates its own area (guest authoritative for local enemies) | works at any distance | large: enemy authority per area, handover on approach, the host's world and the guest's world diverge; the authority records exist (`authority.cpp`) but nothing assigns enemies by area |
| E. Second streaming observer | works at any distance | not available in the engine: one observer, about 60 readers of the manager; memory cost of a second ring of tiles on a PC already near its commit limit |

Recommendation: A (done), B as the next step if the notice is not enough (warp the partner back, limit separation), D only if long-range split play becomes a goal.

Built: `marker_overlay.cpp` draws one line per partner farther than `kBeyondLoadedWorldMetres` (700 m, a margin under the 768 / 800 m table radii) under the toasts.
