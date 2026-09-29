# CODEMAP

Spec: `docs/CONTRACT.md`. Tools: `tools/save_sync_test.py` (two local launchers, checks the save transfer and the reset), `tools/fake_adapter.py` (fake game adapter), `tools/echo_peer.py` (echoes 0x0100/0x0101 frames back after a delay; see `adapters/re0/README.md`), `tools/spoof_peer.py` (sends a wrong slot byte over local transport).

## launcher/ (C# .NET 8, namespace CoopLauncher)

| file | owns | key members |
|---|---|---|
| Program.cs | entry point: update check, then GUI (no args) or CLI, error boundary | `Main`, `RunCli` |
| CliOptions.cs | argument parsing | `CliOptions.Parse`, `Usage` |
| App.cs | wiring and ~100 Hz main loop; CLI runs one session, interactive (GUI) takes commands and returns to idle | `Run`, `Host`, `Join`, `Leave`, `Invite`, `Stop`, `StatusChanged`, `EndSession` |
| AppStatus.cs | display status of the loop (idle, connecting, hosting, joined, game running, peer connected + RTT) | `AppStatus`, `AppState` |
| Updater.cs | self-update from the rolling GitHub release `latest` (`update.json` repo, `version.txt` build): download, rename old files to `*.old`, copy new, relaunch; skipped silently on any failure or outside the packaged layout | `TryInstall` |
| Gui/GuiHost.cs | GUI entry: hides the console, runs App on a background thread, window on the STA thread | `Run` |
| Gui/MainForm.cs | the window: game picker, Host, code + Join, lobby code + Copy + Invite, status, Leave, log pane | `MainForm` |
| Gui/StatusText.cs | status line text | `Format` |
| Session.cs | slots, epochs, membership diffs, frame routing | `ApplyMembership`, `OnPeerFrame`, `OnAdapterFrame`, `End` |
| SlotAssigner.cs | owner = 0, rest sorted by id | `Assign` |
| LoopbackBridge.cs | adapter TCP link: HELLO check, heartbeat, timeout, relay | `Pump`, `Send`, `AdapterReady`, `GameFrame` |
| Framing.cs | frame record, wire/loopback codec, message type constants | `Frame`, `Msg`, `Framing` |
| ControlMessages.cs | control payload builders/parsers | `Welcome`, `PeerUp`, `TryParseHello` |
| PeerStats.cs | ping schedule, EWMA RTT, stats report | `Tick`, `OnPong` |
| ITransport.cs | peer carrier interface | |
| SteamTransport.cs | ISteamNetworkingMessages channel 0, member-only sessions | `Send`, `Pump` |
| LocalTransport.cs | UDP loopback transport + presence keepalive | `PeerPresent` |
| ILobby.cs | membership source interface | |
| SteamLobby.cs | Steam lobby create/join, cf_* data | `Create`, `Join` |
| LocalLobby.cs | fake lobby for local testing | |
| SteamBootstrap.cs | SteamAPI init/callbacks, overlay invites | `PendingInviteLobby` |
| GameProfile.cs | `games/<id>.json` loader, optional `saveSync` block | `Load`, `ListIds` |
| SaveSyncCoordinator.cs | save sync per session: host sender or guest receiver, launch gate, 60 s timeout | `Create`, `Ready`, `TimedOut`, `EnableAdapter` |
| LogForwarder.cs | guest streams new adapter-log bytes to the host every 2 s (LOG_APPEND 0x0050); host appends them to `peer_<steamid>.log` beside its adapter log | `Create`, `Pump` |
| SaveSender.cs | host: paced FILE_BEGIN/CHUNK/END per new peer, resend until ACK ok | `SendTo`, `OnAck`, `Pump` |
| SaveReceiver.cs | guest: temp file, sha256 check, move into session dir, FILE_ACK | `OnFrame`, `Complete` |
| FileMessages.cs | FILE_* payload builders/parsers | `Begin`, `Chunk`, `TryParseBegin` |
| AdapterSettings.cs | `coop=` in the adapter ini, session dir cleanup | `EnableCoop`, `Reset` |
| GameLauncher.cs | adapter install (sha256, `.cfbak`) and Steam game start | `Launch` |
| SteamLibrary.cs | game folder from libraryfolders.vdf + appmanifest | `FindGameDir` |
| RepoPaths.cs | repo root / games dir discovery | |
| Log.cs | timestamped console log; `Written` event feeds the GUI log pane | `Info` |

## adapters/re0/ (C++20, x86, dinput8.dll proxy)

Build (from a VsDevCmd x86 shell): `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build`. Output `build/dinput8.dll`, copied to `adapters/re0/dinput8.dll`. Vendored MinHook in `third_party/minhook` (wrapped by src/hooks.cpp) and Dear ImGui (imgui core + `backends/imgui_impl_dx9`, MIT) in `third_party/imgui`.

| file | owns | key members |
|---|---|---|
| src/proxy.cpp, dinput8.def | DirectInput8Create forwarding, DllMain (spawns init thread, pins module) | `DirectInput8Create`, `DllMain` |
| src/init.cpp | wait for code decryption, start subsystems, SEH boundary | `initThread`, `shutdownAdapter` |
| src/protocol.h | loopback frame codec and message constants (ROOM_STATE, MENU_STATE, PARTY_REQUEST, PARTY_MODE) (mirrors Framing.cs) | `proto::encodeFrame` |
| src/net_client.cpp | launcher link thread: HELLO, heartbeat, reconnect, queues | `NetClient::send`, `poll` |
| src/state_sync.cpp | 60 Hz PLAYER_STATE send for the OWNED character (characterId, hp, senderIsHost, room) plus `focusedCharacterId` (the camera character, read by camera_parity), received-state log, peer join/leave toasts and frame dispatch | `state_sync::start`, `PlayerState` |
| src/game.h | RE0 addresses/offsets (players, enemies, `HitInfo`), SEH-safe reads and writes | `game::controlled`, `partner`, `readTransform`, `callThiscall`, `setThink` |
| src/game_tick.cpp | MinHook on `uPlayerBase::move`; runs registered callbacks once per frame before the controlled player moves, SEH-guarded; per-player move scope hook and a guarded post-move hook (`setPostMove`) | `game_tick::addCallback`, `setMoveScope`, `install` |
| src/hooks.cpp | shared MinHook wrapper: one init, retried install (game settling after SteamStub), status logging | `hooks::install`, `remove` |
| src/game_state.cpp | read-only SEH-guarded views: door transition active, menu open, current room (stage << 8 | room), character in the loaded room | `game_state::doorActive`, `menuOpen`, `currentRoom`, `inCurrentRoom` |
| src/door_phase.h | pure door phase predicate (`door_phase::running`) | `door_phase::running` |
| src/door_travel.cpp | door edge: the start is latched level-triggered from the game tick and the net thread (`onNetTick`) (the partner itself travels by the game's follow logic, see party_mode); arrival sends ROOM_STATE (0x0104), forces a position check and calls `floor_items_sync::onArrival`; ROOM_STATE every 2 s; "Room desync" toast after 3 s of mismatch | `door_travel::enable`, `onFrame`, `onNetTick` |
| src/door_sync.cpp | doors on both machines: hooks sDoorLoad::start (0x552b50, door animation then room change); the focused character's owner runs it and sends DOOR_CHANGE 0x010B, the other machine suppresses its own call, focuses the character that went through and runs the peer's on the game tick; a local player whose character is the partner acts on doors/triggers through the act-on-trigger check (0x564070) and takes the camera; F8 doors sent/run/blocked | `door_sync::enable`, `onFrame`, `uninstall`; `camera_parity::holdLocalFocus` |
| src/flag_sync.cpp | story flags (sFlagManager 0xdcc014 +0x20, 0x11c bytes) kept equal: every 6 frames each machine sends changed words as set/clear masks (FLAG_DIFF 0x010C); received changes are written and adopted as known (no echo); F8 flag words sent/applied | `flag_sync::enable`, `onFrame` |
| src/flag_diff.h | pure word diff/apply for flag_sync (unit tested) | `flag_diff::diff`, `apply` |
| src/menu_mirror.cpp | MENU_STATE (0x0105) sent from the net thread on menu open/close and every 2 s while open; hooks `sUnit::updateAll` 0x727b50 and skips it while a peer's menu is open and ours is closed; hooks sSubMenu::open (0x5d9030): a player whose character is the partner gets the focus moved to it locally while the menu is open (inventory perspective), given back on close; snapshots inventories for exchange detection | `menu_mirror::enable`, `onNetTick`, `onFrame` |
| src/control_rule.h | pure control rules (no game, unit tested): `Control`, `PartyMode`, ownership control and the leave-behind lock | `control_rule::byOwnership`, `byPartyMode` |
| src/character_owner.cpp | Billy/Rebecca identification by vptr; fixed ownership (host = Rebecca, first peer = Billy) + OWNERSHIP (0x0102) resent every second; `controlOf` is the one place the control rule (ownership + party mode + focus) is applied; `isRemoteOwned`/`isLocalOwned` are ownership only; `focus(character)` swaps the camera to a character that is the partner | `character_owner::identify`, `find`, `controlOf`, `isRemoteOwned`, `isHost`, `isHostSlot`, `name`, `enable` |
| src/party_mode.cpp | shared party mode TEAM/LEAVE_BEHIND: PARTY_REQUEST 0x0109 (guest to host), PARTY_MODE 0x010A (host to all, reliable, every 2 s), toast on change; mirrors the mode into the game's follow flag sPlayer +0x40 (TEAM = 1) so vanilla doors carry a following partner | `party_mode::current`, `onLocalToggleKey`, `onFrame`, `enable` |
| src/command_input.cpp | polls the game's switch/partner keys from the net thread (~5 ms, independent of move() ticking; raw-key edge detector, foreground flag logged, acted on only when foreground and a peer is connected) and routes them to camera_parity / party_mode; publishes the focused-character gauge | `command_input::enable`, `onNetTick` |
| src/command_log.cpp | party command observability: every press, request and host decision to adapter.log (identical repeats within 1 s dropped) and the panel's last command / last decision | `command_log::press`, `note`, `decide` |
| src/pickup_guard.cpp | MinHook on the pickup action step 0x500d00: phase 1 with the item already removed (guest's FLOOR_TAKE) ends the action (phase 2) instead of the null deref; counts "pickups aborted" | `pickup_guard::enable`, `uninstall` |
| src/key_config.cpp | parses KC_change / KC_trace ("KB_<KEY>") from the game's config.ini into virtual-key codes (defaults V, E) | `key_config::parse` (first KB_ binding of the key in any section), `load`, `virtualKeyOf` |
| src/window_focus.cpp | shared "game window is foreground" check (overlay F8 and command keys) | `gameIsForeground` |
| src/settled_copy.h | change tracking for a fixed memory block: due when changed and stable `kSettleFrames` (10) or on resend interval; `monotonicMs` | `SettledCopy::observe`, `due`, `markSent`, `adopt` |
| src/inventory_sync.cpp | INVENTORY (0x0106): each locally owned character's 0x40-byte sItem block sent after settling and every 5 s; remote-owned blocks overwritten from the queue on the game thread; INVENTORY carries an origin byte: a change to the other player's character made in a local menu (item exchange) is sent once on close (origin 1) and the owner applies it; F8 inventory exchanges | `inventory_sync::enable`, `onFrame`, `framesSinceLocalChange` |
| src/floor_items_sync.cpp | FLOOR_PUT 0x0107 / FLOOR_TAKE 0x0108: MinHooks on `sItemPut::put` 0x4de500 and `remove` 0x4de730; local calls are broadcast after the original, remote-owned move scope suppresses them, network events applied on the game thread with a reentrancy flag; events for another room, or for the current room before a door arrival has settled (30 frames), go to `floor_pending` and apply in order once the room is ready | `floor_items_sync::enable`, `onFrame`, `onArrival`, `uninstall` |
| src/floor_pending.cpp | per-room pending floor events (game thread, no game deps): a take cancels the nearest matching put within 50 units, cap 32 per room drops the oldest | `floor_pending::Queue::add`, `take`, `total` |
| src/camera_parity.cpp | shared camera: guest mirrors the host's controlled character; a local V press becomes SWITCH_REQUEST (0x0103) that the host applies (host and guest requests share `queueSwitch`; every decision is logged with its reason); the host focuses Rebecca whenever the player objects change and undoes a game-side V within 500 ms of an adapter switch | `camera_parity::onFrame`, `onLocalSwitchKey`, `enable` |
| src/partner_think.cpp | gives every co-op controlled (not Vanilla) character on a cPlayerSubThink a cPlayerThink (with `coop=1`); skips while a door runs and 30 frames after, re-read every tick | `partner_think::enable` |
| src/pad_frame.h | PAD_FRAME (0x0101) wire structs and pad vtable slot constants | `pad::PadFrame`, `PadPacket` |
| src/input_record.cpp | per frame: evaluates the original pad vtable queries, sends last 3 frames | `input_record::captureOriginals`, `enable` |
| src/net_pad.cpp | jitter buffer of the peer's frames, cloned pad object with thunk vtable, peer slot | `net_pad::onPacket`, `advance`, `object`, `analog` |
| src/input_redirect.cpp | MinHook on getPad and the analog getter: inside a character's move, Remote reads the NetPad, Locked reads the game's blocked pad, else the real pad | `input_redirect::install`, `realPad`, `realAnalog`, `replayingRemoteInput` |
| src/state_correction.cpp | remote-owned character position: after its move (game_tick post-move hook) it is pulled toward the owner's newest PLAYER_STATE extrapolated by velocity (dead zone 3, blend 0.5 per tick up to 60, snap beyond; same room only); applies its HP (setHP); `requestForcedCheck` snaps once regardless of distance | `state_correction::onFrame`, `enable`, `requestForcedCheck` |
| src/position_blend.h | pure blend/extrapolation/classify math (no game, unit tested) | `position_blend::classify`, `blendPosition`, `blendRotation`, `extrapolate` |
| src/enemy_registry.cpp | sEnemy pool reads: slot to enemy, enemy to slot, enemy vtable check | `enemy_registry::enemyAt`, `slotOf`, `isEnemy` |
| src/damage_thunk.cpp | generated thunk for enemy vtable slot 35 (thiscall ret 0xC in, one stdcall handler out) | `damage_thunk::patchVtable`, `callOriginal` |
| src/enemy_damage_hook.cpp | patches all 38 enemy vtables; ownership rules for local/remote/host/guest hits; reentrancy flag | `enemy_damage_hook::install`, `applyNetworkHit` |
| src/enemy_protocol.h | HIT_REQUEST 0x0110, HIT_APPLIED 0x0111, ENEMY_STATE 0x0112 constants and payload structs | `HitPayload`, `EnemyEntry` |
| src/enemy_net.cpp | hit messages: guest request, host apply and announce, guest apply; game thread queue | `enemy_net::requestHit`, `announceHit`, `onFrame` |
| src/enemy_state.cpp | host 10 Hz enemy snapshot; guest HP (setHP) and position snap corrections | `enemy_state::onFrame`, `enable` |
| src/player_damage.cpp | HP/death ownership: MinHook gates on `setHP` 0x529310 and `cPlayerThink::onDeath` 0x4fcea0 (remote-owned characters only change via the owner); PLAYER_DIED 0x0120 replays remote deaths; authoritative `setHp` for all sync code | `player_damage::install` |
| src/vtable_tracer.cpp | counting thunks patched into vtables, 2 s report | `vtable_tracer::install`, `uninstall` |
| src/thunk_emit.h | shared x86 byte writers for generated thunks | `thunk::emit`, `emitAddress` |
| src/remote_storage_proxy.cpp | 64-slot interface proxy: per-real-pointer forwarding thunks, slot overrides | `remote_storage_proxy::get` |
| src/save_redirect.cpp | patches the SteamRemoteStorage IAT slot (0xcb1458); FileWrite/Read/Delete/Exists/GetFileSize served from `coop/session` | `save_redirect::install`, `uninstall` |
| src/config.cpp | `coop/adapter.ini` (port, trace, trace_vtables, coop, overlay) | `loadConfig` |
| src/debug_stats.cpp | thread-safe status registry (atomic counters with per-second rates, gauges, session, disabled callbacks, last error, last command / decision notes); modules write, the overlay reads | `debug_stats::count`, `set`, `setSession`, `setError`, `snapshot` |
| src/debug_lines.cpp | turns a stats snapshot into the panel lines (label, value, red flag) | `debug_lines::build` |
| src/d3d9_hook.cpp | inline-hooks d3d9!Direct3DCreate9 before the decryption wait, wraps CreateDevice, hooks the game device's EndScene/Reset | `d3d9_hook::install`, `uninstall` |
| src/debug_overlay.cpp | Dear ImGui (DX9 backend, draw-only, no input) panel drawn in EndScene, height capped to the screen and scrolled with Page Up / Page Down; lazy init on first show or toast, panel hidden until F8, toasts (top-center, fading, max 3) always, SEH-guarded (a fault disables the overlay for the session); Reset invalidates/recreates device objects | `debug_overlay::install`, `framesSeen`, `framesDrawn`, `disabled`, `setVisible`, `toast` |
| src/toast_queue.cpp | mutex-guarded timed message queue behind the overlay toasts; no ImGui | `toast_queue::push`, `visible` |
| src/log.cpp, src/paths.cpp | `coop/adapter.log`, game/coop dirs | `logger::write`, `writeUnlessRepeated`, `coopDirectory` |
| tests/proxy_test.cpp | x86 exe: fake 64-slot interface, checks thunk forwarding, overrides, caching | |
| tests/damage_thunk_test.cpp | x86 exe: fake vtable with a ret 0xC slot, checks thunk arguments, pass-through, suppression and stack balance | |
| tests/command_rules_test.cpp | x86 exe: config.ini key parsing (including the real `[JOYPAD]` KC_change=KB_V / KC_trace=KB_E text) and the control truth table (no game) | |
| tests/door_phase_test.cpp | x86 exe: door phase predicate (0..4 running; 5 and -1 idle) | |
| tests/flag_diff_test.cpp | x86 exe: flag word diff set/clear masks, apply round trip, out-of-range word | |
| tests/position_blend_test.cpp | x86 exe: classify thresholds, blend convergence, extrapolation cap, quaternion shorter arc (no game) | |
| tests/settled_copy_test.cpp | x86 exe: settle delay, resend, adopt and reset of `SettledCopy` (no game) | |
| tests/floor_pending_test.cpp | x86 exe: put/take coalescing, per-room cap and drop, ordering, room isolation of `floor_pending::Queue` (no game) | |
| tests/overlay_test.cpp | x86 exe: real windowed D3D9 device, installs the overlay hook, renders frames with fake stats, toasts (panel hidden and shown) and a Reset, checks the hook fires (skips without a device) | |
| tests/net_test.cpp | x86 exe driving `NetClient` against a launcher, checks PLAYER_STATE and PAD_FRAME round trips | `--port --seconds --expect-peer` |

## release/ (repo root)

| file | owns |
|---|---|
| .github/workflows/release.yml | on push to main: build adapter (x86), publish launcher (self-contained), assemble the package (`launcher\app`, `launcher\games\re0.json`, `adapters\re0\dinput8.dll`, `README.txt`, `version.txt` = run number, `update.json` = this repo), zip `RE0-Coop.zip`, replace the release tagged `latest` (title `Build <run number>`) |
| launcher/package/README.txt | player README shipped in the package |
| launcher/update.json | `{ "repo": "Precyse/RE0Together" }` copied next to the exe; a missing file or the `OWNER/REPO` placeholder disables updating (the workflow writes the real repo into the package) |

## Where to look for

- GUI: `launcher/Gui/MainForm.cs`; session logic stays in `App.cs`
- Auto-update or release packaging: `Updater.cs`, `.github/workflows/release.yml`
- Wire format or control messages: `Framing.cs`, `ControlMessages.cs`
- Slot / epoch rules: `Session.cs`, `SlotAssigner.cs`
- Adapter link behaviour: `LoopbackBridge.cs`
- Add a game: new `launcher/games/<id>.json`
- Native `steam_api64.dll` (from a Steamworks SDK 1.60 compatible copy): `launcher/native/`
- Items (inventory blocks): `inventory_sync.cpp`; floor items: `floor_items_sync.cpp`; layouts in `docs/RE0_NOTES.md` Items
- RE0 game addresses: `adapters/re0/src/game.h` (source: `docs/RE0_NOTES.md`)
- Save sync: `SaveSyncCoordinator.cs` (launcher), `save_redirect.cpp` (adapter)
- Guest logs on the host: `LogForwarder.cs`, written to `<game>\coop\peer_<steamid>.log`
- Adapter network behaviour: `adapters/re0/src/net_client.cpp`
- Control rule (who drives a character): `control_rule.h` (pure) applied in `character_owner::controlOf`; party commands: `command_input.cpp`, `party_mode.cpp`, `camera_parity.cpp` (trace a press or decision in `adapter.log`, lines starting `command:`); pickup crash guard: `pickup_guard.cpp`
- Overlay contents: `adapters/re0/src/debug_lines.cpp` (add a stat: counter/gauge in `debug_stats.h`, update it where the event happens, add a line)
- Reverse-engineering patterns shared across games: `docs/re/PATTERNS.md` (grown by the `coop-re` skill)
