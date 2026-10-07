# RE0 co-op gaps (audit 2026-10-06)

What a real two-player session hits that the single-player game assumes away. Each candidate was checked against `main` (867c42c). Status: **done** (code exists, may be unverified live; nothing here ran on two PCs), **partial**, **missing**, **not needed**. Enemy items belong to the agent re0-sync.

## Last night's problems, for reference

| problem | status | owner file |
|---|---|---|
| Late arrival in a split room finds it empty (dormant record reused, no room load) | done, retest pending | `adapters/re0/src/split_rooms.cpp` (`beforeLocalDoor`, `onArrival`) |
| Both characters spawn inside each other at a door | done, unverified live | `spot_rule.h`, `scene.cpp` |
| One shared camera in Team mode | done | `camera_parity.cpp` |
| Item exchange between the two players | done (origin byte, applied by the owner) | `inventory_sync.cpp`, `menu_mirror.cpp` |
| Auto-join pressing Enter forever with a muted keyboard | done (off by default, six-press give-up) | `auto_join.cpp`, `config.cpp` |
| Steam-overlay-only invites, joins failing silently | launcher-join / launcher commits 867c42c, 33996be | `launcher/` |

## Full candidate list

### Session flow
| candidate | status | owner file | note |
|---|---|---|---|
| Host, join, lobby code | done | `launcher/App.cs`, `SteamLobby.cs` | |
| Guest catches up on join (rooms, inventories, flags) | done | `join_sync.cpp` | re-requested after every load (pair change resets it) |
| Rejoin after a network drop | done | `launcher/Rejoin.cs`, `join_sync.cpp` | TESTPLAN rows 16, 22 |
| Rejoin after the guest's game crashed | partial | `launcher/App.cs`, `GameLauncher.cs` | the launcher never notices the game exited and offers no relaunch; restarting RE0 from Steam reconnects and catches up (untested) |
| Guest leaves mid-room | done | `partner_think.cpp`, `character_owner.cpp` | Billy returns to partner AI |
| Host leaves or crashes | done, unverified live | `state_sync.cpp`, `partner_hud.cpp` | "Host left: not saved" toast and status line, saves refused; the guest keeps playing the session copy (new lobby code for the guest: launcher section) |
| Save ownership: host saves, guest receives | done | `session_slot.cpp`, `save_redirect.cpp`, `SaveSyncCoordinator.cs` | SAVE_CHANGED resends on every host save |
| Guest saves at a typewriter | done, unverified live | `session_slot.cpp` | refused with a toast; whether the ribbon is spent before the request is not known (RE0_NOTES, QoL pass) |
| Launcher build mismatch | done | `launcher/BuildCheck.cs` | |
| Game build mismatch (Steam updates re0hd.exe) | missing | `launcher/SteamLibrary.cs`, `GameProfile.cs`, `adapters/re0/src/init.cpp` | adapter hardcodes build 17178773 addresses; nothing checks the installed build |

### Gameplay sync
| candidate | status | owner file | note |
|---|---|---|---|
| Level-placed item pickups (ammo, herbs, key items on the map) | done, unverified live | `floor_items_sync.cpp` | map items are sItemPut records put by the room script; pickups already crossed as FLOOR_TAKE, the script's puts were wrongly announced as FLOOR_PUT (duplicates) and no longer are |
| Dropped floor items | done | `floor_items_sync.cpp`, `floor_pending.cpp` | |
| Floor items for a late joiner or rejoiner | done, unverified live | `floor_items_sync.cpp`, `join_sync.cpp` | FLOOR_SNAPSHOT journal (changes since the last save or load); lost when the host restarts |
| Equipped weapon | done, unverified | `equip_refresh.cpp` | |
| Story flags, puzzle progress, doors unlocked once, events seen | done | `flag_sync.cpp` | the 0x47 flag words, which include the enemy-killed bits |
| Partner health and HP changes (HUD shows both characters) | done | `state_sync.cpp`, `state_correction.cpp`, `player_damage.cpp` | only the owner changes a character's HP |
| Cutscenes and scripted moves of the other character | done | `event_place.cpp` | the story cutscene itself seen by both is a separate row below |
| Story cutscene seen by both, scripted room events (spawns, window breaks, camera cuts) | done, unverified live | `event_sync.cpp`, `event_rule.h` | the firer's room script thread is replayed op by op on the peer when it is in the room (docs/re/RE0_EVENT_SYNC.md) |
| Puzzle props (push blocks, dumbwaiter, cranks) | partial | `flag_sync.cpp` | the flags they set are synced; objects whose state lives in the unit (pushable or shootable props, moving platforms, doors that animate after a script) are not; not audited object by object |
| Using a healing item on the partner | unknown | `player_damage.cpp` | the setHP gate refuses non-owner HP changes; check whether the game offers it and whether the herb is lost |
| Enemies the same on both screens: both machines run their enemies; decisions come only from the owner (the follower takes them at its own think step or action boundary), hits and deaths are owner-decided and replayed exactly, the target follows the owner, rooms start together (barrier) | done, unverified live | `enemy_decision.cpp`, `enemy_net.cpp`, `enemy_state.cpp`, `enemy_target.cpp`, `enemy_damage_hook.cpp`, `room_gate.cpp` | design, ownership and conditions: docs/re/RE0_ENEMY_SYNC_REVIEW.md; enemies are created from the same seeded random state on both machines (`enemy_spawn.cpp`) |
| Enemy classes outside the 15 base ones (23 vtables: uEnemy2a..47) | done for 18, unverified live | `enemy_decision.cpp` | 18 take the owner's actions at their action executor's boundary; uEnemy35, 44, 45, 47 and 3bRebecca decide natively (reasons: docs/re/RE0_ENEMY_SYNC_REVIEW.md section 7); watch `enemy_state: slot N drift D` lines |
| Enemies killed in one player's room stay dead for both | done, unverified live | `flag_sync.cpp` | an enemy reads bit `0x612 + spawn id` at its first update, scripts set it, `flag_sync` carries it (RE0_NOTES, kill persistence) |
| Enemy state on reunion in a shared room (authority hand-over) | done, unverified live | `door_travel.cpp` (`enemyAuthority`), `room_gate.cpp` | entering together: the door barrier starts both rooms at once; walking in later: the native enemies are corrected toward the owner's; leaving: nothing switches off, the enemies just keep running |
| Boss fights (multi-part enemies, scripted phases) | re0-sync | `enemy_*` | |

### Player-facing QoL
| candidate | status | owner file | note |
|---|---|---|---|
| Partner name, health condition, same/other room | done, unverified live | `partner_hud.cpp`, `partner_status.cpp` | top-right line; condition from hp against the highest hp seen |
| Partner location while apart | done (status line), map unchecked | `partner_hud.cpp` | the status line names the partner's room (`room 0x24`); whether the vanilla map (Q) also shows it needs a live look |
| Ping or marker | skipped | none | low value in a fixed-camera game; the status line names the partner's room |
| Partner-left notice | done | `state_sync.cpp` | the leave toast names the player; host leaving has its own notice |
| Pause behaviour | done, unverified live | `menu_mirror.cpp`, `menu_hold_rule.h` | menus hold the partner's world 20 s, reading screens and cutscenes until closed |
| Item box | not needed | none | RE0 has none; items are dropped on the floor (floor sync covers it) |
| Downed or revive | not needed | none | RE0 has no such state; a death is a game over |
| Local script flags (sEventScript +0x58) | not needed | none | script-internal ordering, not saved; a replayed thread sets them on the peer itself (`event_sync`) |
| Controls overlay | missing | `launcher/package/README.txt` | the user's rule is no explainer text in UI; keep keys in the README only |

### Failure handling
| candidate | status | owner file | note |
|---|---|---|---|
| One player dies | done | `player_damage.cpp` | PLAYER_DIED replays the death, both get game over |
| Game over, continue | done, unverified live | `auto_join.cpp`, `session_rule.h` | the guest's Enter at the Dead phase works with auto_join off |
| Desync recovery | done, unverified live | `resync.cpp`, `door_travel.cpp` | `coop\resync_now.txt` and an automatic retry after 10 s |
| Game crash on one side | partial | `crash_dump.cpp`, `LogForwarder.cs` | dump reaches the host; no relaunch path (see Session flow) |

### Setup QoL
| candidate | status | owner file | note |
|---|---|---|---|
| First run | partial | `launcher/` | launcher-app is building settings; see the launcher section for what it will not cover |
| Config | done | `config.cpp`, `adapter.ini` | |
| Logs to send | partial | `LogForwarder.cs` | guest logs and dumps reach the host; the launcher's own log is never written to a file; no one-click bundle |

## Top 10 (missing or partial), ranked by player impact over effort

### 1. A guest's typewriter save goes nowhere (S, new) (done, unverified live, QoL pass 2026-10-06)
- **Root change:** on a guest, refuse the Save room phase's save request (the hook on 0x613390 already sees it) and toast "Only the host can save"; check first whether the ink ribbon is taken before 0x613390 runs and, if so, refuse at the typewriter interaction instead so the ribbon stays.
- **Files:** `adapters/re0/src/session_slot.cpp`, `docs/RE0_NOTES.md` (where the ribbon is consumed).
- **Verify:** `tools/re0/fake_session.py --guest` makes the game the guest; save at a typewriter; adapter.log shows the refusal, slot 20 in `coop\session` keeps its hash, ribbon count unchanged.

### 2. Installed game build is never checked (S, new; shared with DS2)
- **Root change:** profiles list supported Steam build ids (`supportedBuilds` in `games/re0.json`); the launcher reads `buildid` from the appmanifest it already parses, refuses to start an unsupported build with the numbers, and sends the game build in BUILD_INFO so host and guest must match.
- **Files:** `launcher/SteamLibrary.cs`, `GameProfile.cs`, `BuildCheck.cs`, `games/re0.json`, `docs/CONTRACT.md`.
- **Verify:** `tools/build_check_test.py` extended with a game build field; a profile with a wrong build id refuses with both numbers.

### 3. Partner status line (S/M, new) (done, unverified live, QoL pass 2026-10-06)
- **Root change:** an always-on one-line overlay item: partner name, condition (Fine / Caution / Danger from hp), "same room" or the partner's room, and "in menu" while their MENU_STATE is open. Data only, no hint text.
- **Files:** new `adapters/re0/src/partner_hud.cpp` (reads `state_sync` last state, `door_travel::peerPlace`, `menu_mirror`), drawn from `debug_overlay.cpp`; name from `net_client` PEER_UP.
- **Verify:** `tools/echo_peer.py` supplies PLAYER_STATE with hp and room; `tests/overlay_test.cpp` renders it with fake values; first check the vanilla map (Q) for the partner's room.

### 4. Host leaving ends the session silently for the guest (S, new) (done, unverified live, QoL pass 2026-10-06)
- **Root change:** when the host slot goes down (epoch bump), the guest gets a lasting toast "Host left: this game is not saved" and guest saves stay refused; the leave toast carries the name for either side.
- **Files:** `adapters/re0/src/state_sync.cpp`, `session_slot.cpp`.
- **Verify:** `fake_session.py --guest`, then stop the script: the toast and log line appear; overlay_test for the text.

### 5. Resync on demand (S, new) (done, unverified live, QoL pass 2026-10-06)
- **Root change:** `coop\resync_now.txt` (same convention as DS2's `resync_trigger`) and an automatic retry when "Room desync" persists 10 s: the guest re-sends SNAPSHOT_REQUEST, the host re-sends both inventories and the full flag block.
- **Files:** `adapters/re0/src/join_sync.cpp`, `door_travel.cpp`, `inventory_sync.cpp`, `flag_sync.cpp`.
- **Verify:** `fake_session.py --guest`: create the file; the fake host logs a second SNAPSHOT_REQUEST and the game reapplies it.

### 6. Game over with auto_join off (S, new) (done, unverified live, QoL pass 2026-10-06)
- **Root change:** keep auto_join's virtual Enter for the Dead phase only (the Continue cursor is the default there and the host's choice is known from SAVE_SLOT), so a guest follows the host's Continue without the title-screen risk; update TESTPLAN row 13.
- **Files:** `adapters/re0/src/auto_join.cpp`, `docs/TESTPLAN_RE0.md`.
- **Verify:** pull the decision into a pure rule (phase, host phase, controlled) with a unit test in `tests/command_rules_test.cpp`; `fake_session.py` gains a `phase <n>` command to announce the host's SAVE_SLOT phase.

### 7. Unbounded world freeze under the partner's menu (S, new) (done, unverified live, QoL pass 2026-10-06)
- **Root change:** freeze only for reading screens and cutscenes without limit; for SubScreen and Option (inventory, Esc pause) release after 20 s and show "Partner in menu" in the status line instead.
- **Files:** `adapters/re0/src/menu_mirror.cpp`, `room_phase.h`.
- **Verify:** pure freeze rule (phase, seconds open) with a unit test; `fake_session.py` sending MENU_STATE open for 30 s.

### 8. Level-placed items picked up twice (L, new; done as a small fix, see RE0_NOTES "Level-placed items")
- **Root change:** find the stage item pickup (0x4dfa00 reads the stage item table; the add on pickup at 0x4dc269 is the entry point) and the per-item taken flag; send STAGE_ITEM_TAKE {scene, item index}; the receiver removes the item through the game's own path (or sets the flag and hides the unit), queued per room like `floor_pending`.
- **Files:** new `adapters/re0/src/stage_items_sync.cpp`, `game.h`, `docs/RE0_NOTES.md`; reuse `floor_pending.cpp`.
- **Verify:** `tools/re0/watch_write.py` on the flag during one pickup (coop-re skill); then `fake_session.py` command `take <scene> <index>` and a screenshot of the item gone.

### 9. Floor items missing for a late joiner (M, new) (done, unverified live, QoL pass 2026-10-06)
- **Root change:** JOIN_SNAPSHOT carries the host's live sItemPut entries (itemId, count, position, rotation) and its pending floor events of other rooms; the guest replays them through `sItemPut::put` after catching up.
- **Files:** `adapters/re0/src/join_sync.cpp`, `floor_items_sync.cpp`, `floor_pending.cpp`, `tools/re0/fake_session.py`.
- **Verify:** `tests/floor_pending_test.cpp` for the merge; `fake_session.py --guest` snapshot with two floor items, both appear.

### 10. The other player never sees a story cutscene (L, new)
- **Root change:** find the EventDemo start call and its event id; the machine that starts one sends EVENT_START {scene, id}; the other machine, if in the same room, starts the same event (it already freezes while waiting); apart, it keeps playing.
- **Files:** new `adapters/re0/src/event_sync.cpp`, `phase_watch.cpp`, `event_place.cpp`, `docs/RE0_NOTES.md`.
- **Verify:** trace the start with `tools/re0/disasm.py` and one solo cutscene; replay it from a `fake_session.py` command `event <id>`.

## Launcher (not covered by launcher-app's settings, both-mods rail and button audit)

| item | status | files | size |
|---|---|---|---|
| Game build check per profile, host and guest compared (top 10 item 2) | missing | `SteamLibrary.cs`, `GameProfile.cs`, `BuildCheck.cs` | S |
| The game closing or crashing mid-session is not noticed: status stays "game running", no Relaunch (keep the session folder, reinstall, start, the join snapshot catches up) | missing | `App.cs`, `AppStatus.cs`, `GameLauncher.cs` | M |
| A game started outside the launcher (vanilla, no adapter link) is skipped with a log line only; after 20 s with the game running and no HELLO, say so in the status | missing | `App.cs`, `LoopbackBridge.cs`, `Gui/StatusText.cs` | S |
| The launcher's own log is never written to a file | missing | `Log.cs` | S |
| One action that zips adapter.log, peer logs, crash dumps, the launcher log, adapter.ini and version.txt for sending | missing | new `ReportBundle.cs` | S |
| Host crash: the guest must get a new lobby code; follow the host into its next lobby through Steam rich presence (check with launcher-join first) | done in launcher-app (HostFollow, rich presence); untested with two accounts | `SteamBootstrap.cs`, `SteamLobby.cs` | M |
