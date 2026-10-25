# CODEMAP: ds2

Death Stranding 2: the x64 adapter. Index and "where to look for X": `CODEMAP.md`.

## adapters/ds2/ (C++20, x64, version.dll proxy, DEATH STRANDING 2)

Build (from a VsDevCmd `-arch=amd64` shell): `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build`. Output `build/version.dll`, copied to `adapters/ds2/version.dll`. Unit tests (no game): `ctest --test-dir build`. Shares the launcher link, logging, crash dumps, toasts, position blending, the MinHook wrapper and the vendored MinHook / Dear ImGui (now with `backends/imgui_impl_dx12`) with `adapters/re0` (compiled from `../re0/src` and `../re0/third_party`). Engine-specific code sits behind `src/game.h`; `src/decima/` holds engine types shared by every Decima game, `src/ds2/` this game's build.

| file | owns | key members |
|---|---|---|
| src/proxy.cpp | the 17 version.dll exports forwarded to the system DLL by linker directives | `DllMain`, `hostIsGame` |
| src/init.cpp | start-up: crash dumps, config, the build guard | `initThread`, `simulationTick` |
| src/build_guard.cpp | compares DS2.exe's PE TimeDateStamp and SizeOfImage with the build the fixed addresses were taken from (Steam 23923251); a mismatch installs no hooks | `build_guard::checkRunningGame`, `identityOf`, `supported`, `kSupported` |
| src/partner_status.cpp | the PLAYER_STATE status word: health byte and dead/down/loading/driving/menu flags, its texts, the died / back transition and `away` (dead or loading parks the partner's body) (unit tested) | `partner_status::encode`, `decode`, `stateText`, `healthPercent`, `transition`, `away` |
| src/gear_snapshot.cpp | the guest's gained gear as a piece list (slot, kind, category, durability): multiset `minus`, text `format` / `parse` (unit tested) | `gear_snapshot::minus`, `format`, `parse` |
| src/gear_restore.cpp | adapter.ini `gear_restore=1` (off by default): while a guest, writes what Sam gained since the join (backpack and worn pieces) to `coop/personal_gear.txt` every 30 s on change | `gear_restore::installEarly`, `tick`, `setEnabled` |
| src/ds2/local_status.cpp | the local player's health (entity GetHealth through enemy_vitals), dead flag (an unreadable entity is unknown, not dead), loading screen, driving and menu | `game::localStatus` |
| src/config.cpp | `coop/adapter.ini` (port 27980, overlay, self_marker, remote_body = the partner's body, enemy_sync = shared enemies and weapon_sync = weapon sync, all three on by default; weapon_attach_mode) | `loadConfig`, `kFlags` |
| src/documents_redirect.cpp | session saves: with `coop\session\Documents` present, the game's import slots for SHGetKnownFolderPath / SHGetFolderPathW return that folder for Documents (patched from DllMain) | `documents_redirect::install`, `active` |
| src/import_patch.cpp | points one of the game executable's own import slots at a detour (save redirect, raw input filter) | `import_patch::redirect` |
| src/input_filter.cpp | redirects the game's GetRawInputData import: key presses an adapter menu claims reach the game as an unknown key, releases always pass | `input_filter::install` |
| src/game.h | the engine interface the rest of the adapter uses | `game::resolve`, `watchInteractions`, `blockScriptedInteractions`, `drivenVehicle`, `placeVehicle`, `vehicleCargo`, ... |
| src/ds2/game.cpp | DS2 implementation: byte patterns for Player::GetLocalPlayer (PlayerManager global) and Player::GetLastActivatedCamera (camera stack offsets); Player -> Entity -> WorldTransform | `findCode`, `localPlayerObject`, `lastActivatedCamera` |
| src/decima/world_transform.h | Decima WorldPosition (doubles) / RotMatrix / WorldTransform layouts with static_asserts | `decima::WorldTransform` |
| src/decima/safe_read.cpp | SEH-guarded game memory reads | `decima::safeRead`, `readPointer` |
| src/pattern_scan.cpp | byte-pattern search in the game's .text, rip-relative targets | `pattern_scan::find`, `ripTarget` |
| src/player_sync.cpp | a guest that hears the host announced again (a new join serial after the first, so the host was unreachable and came back inside the launcher's grace) asks `resync::kAll` from the host | `player_sync::start`, `remotePlayers`, `PlayerState` |
| src/world_to_screen.h | pure pinhole projection from a horizontal field of view (unit tested), plus the off-screen edge arrow | `world_to_screen::project`, `edgeArrow` (screen-edge position and direction of an off-screen point) |
| src/dx12_hook.cpp | hooks CreateSwapChain(ForHwnd) (remembers the game's queue), ExecuteCommandLists (fallback queue), Present / Present1 | `dx12_hook::install` |
| src/ds2/frame_gen.h/.cpp | the game's frame generation setting (registry HKCU ...\Graphics FrameGen: 0 off, 1 DLSS, 2 FSR, 3 XeSS) | `frame_gen::current`, `fromValue`, `overlaySafe` |
| tests/frame_gen_test.cpp | value mapping and which modes keep the overlay |  |
| src/remote_body.h | the partner's body API: `setEnabled`, `installEarly` (engine hooks, on unless adapter.ini says `remote_body=0`), `setTarget` | `remote_body::setTarget`, `installEarly` |
| src/ds2/remote_player.cpp | the partner's body, a second player entity | `remote_body::*`, `remote_player::entity`, `player`, `slot`, `isLive`, `ds2::setEntityVisible`, ... |
| src/ds2/remote_camera.cpp | the remote's own DSThirdPersonPlayerCameraComponent and DSCameraMode (index patched to -1 for the mode init); skips the component's Init / Update / Teleported and the mode builder for the remote | `remote_camera::give`, `SpawnScope`, `installEarly` |
| src/ds2/remote_baggage.cpp | detour on the baggage carrier registration 0x14123bad0 | `remote_baggage::installEarly`, `beginSpawn`; the carriers registered while this thread is in remote_camera's SpawnScope are the remote's |
| src/ds2/remote_marker.cpp | the marker manager (global 0x14623E638) keeps groups of one marker (player marker type 1, backpack marker type 25) | `remote_marker::snapshot`, `repair` |
| src/ds2/remote_guards.cpp | player id table sized 4, art part toggle bound check for the remote, the components that write the shared HUD block / act on Sam silenced | `remote_guards::installEarly`, `silenceRemote` |
| src/ds2/remote_update.cpp/.h | what the remote body's DSPlayerComponent update runs (it starts the rides but also flickers Sam's prompts) | `remote_update::installEarly`, `setSkipMask` |
| src/ds2/player_system_guard.cpp/.h | DSPlayerSystem's singleton pointers: every load of the system pointer inside the equipment-manage and facial-rig inits | `player_system_guard::installEarly`, `setEquipmentUpdateSkipped` |
| src/ds2/remote_appearance.cpp | ticks the remote's body-variant loader after Sam's, asking for Sam's variant once (the costume) | `remote_appearance::installEarly`, `onSpawned` |
| src/ds2/remote_animation.cpp | the remote copies another player's animation | `remote_animation::installEarly`, `setMirrorLocalPlayer` |
| src/ds2/remote_ride.cpp |  | `remote_ride::tick`, `holdsBody`, `seatLocalPassenger`, `endLocalRide` |
| src/ds2/ride_plugin.h | field offsets of the DSPlayerRideVehicleActionPlugin (phase, requested phase, target, owner) and of the owner's ride request, shared by remote_ride and ride_probe | `ride_plugin::kPhase`, `kOwnerRequestKind` |
| src/ds2/ride_probe.cpp | probe P1, log-only, on only with adapter.ini `diagnostics=1` | `ride_probe::installEarly` |
| src/ds2/remote_context.cpp | while the entity dispatcher runs for the remote, the helpers that answer "the player with local index 0" (controller, param query, controlled entity, notify, send) answer for the remote | `remote_context::enter`, `leave`, `active`, `install` |
| src/ds2/setdriver_guard.cpp | Vehicle::SetDriver for the remote keeps the vehicle manager's local driven vehicle and last-driven ids as they were | `setdriver_guard::install`, `localPassengerVehicle`, `endLocalPassenger` |
| src/ds2/player_state.cpp | a player entity's DSPlayerState: its ride plugin, whether its state machine runs, and `GameplayClock` | `ds2::ridePlugin`, `inGameplay`, `GameplayClock` |
| src/ds2/engine.h | `ds2::at` (file VA to live address), `field<T>`, entity field offsets (transform, parent, UUID, weak target), component lookup by type record, the entity a weak pointer refers to | `ds2::at`, `field`, `componentByRecord`, `weakEntity` |
| src/ds2/body.cpp | moving a body to a pose with ds2::placeEntity | `game::placeBody` |
| src/ds2/place.cpp | moving an entity like the game's scripts: Entity::SetWorldTransform by byte pattern, then the mover's SetVelocity (bodies and vehicles) | `ds2::placeEntity`, `setEntityVisible`, `entityTransform` |
| src/ds2/vehicle.cpp | DS2 vehicles: DSVehicleManager by byte pattern (MovePlayerDrivingVehicle), its DSVehicleContentInfo records (+0x30, 0x110 apart: id +0x8, entity +0x88), the driven entity at +0x79E0 +0xF8 | `game::drivenVehicle`, `vehiclePose`, `placeVehicle`, `ds2::loadedVehicle` |
| src/msvc_rtti.cpp | in-process MSVC RTTI: primary vtable of a class by name | `msvc_rtti::vtableOf` |
| src/ds2/use_gate.cpp | guest restriction: hooks the entity message dispatcher at start-up, keeps the active sequence-network use location instances | `game::watchInteractions`, `blockScriptedInteractions` |
| src/decima/entity.cpp | an entity's component by primary vtable or by a test | `decima::findComponent`, `findComponentWhere` |
| src/decima/localized_text.cpp | the UTF-8 text of a LocalizedTextResource | `decima::localizedText` |
| src/ds2/world_facts.cpp | hooks the FactDatabase writers (bool 0x1401499c0, int 0x14064d080) | `world_facts::installEarly`, `game::shareFactWrites`, `takeFactWrites`, `factSnapshot`, `applyFact` |
| src/ds2/order_gate.cpp | guest orders: hooks the terminal's start command (DSGameMenuCommandReservedStartMission execute 0x1413f0520 and the controller commit 0x1413effe0) | `game::watchOrders`, `blockOrders` |
| src/ds2/cargo.cpp | DS2 cargo: DSBaggageManager by byte pattern (CreateAndAddBaggageToPlayer, DeleteBaggage, the manager's create and its create info) | `game::lockerContents`, `lockerPieces`, `addLockerPiece`, `lockerOfSlot`, `isLockerOwner`, `privateLockers`, ... |
| src/cargo_transfer.cpp | host-decided cargo moves: every player reports its backpack to all | `cargo_transfer::onFrame`, `tick`, `give`, `take`, `partner`, `partnerLoad`, ... |
| src/cargo_ground.cpp | loose cargo kept the same in both worlds: each player diffs its backpack against the loose pieces seen around it in the last 10 s (500 ms watch; a piece passes through the hands) | `cargo_ground::onFrame`, `tick` |
| src/load_overlay.cpp | the partner's load on their body: one box per reported piece, stacked two wide up the back, projected through the game camera (drawn over the frame, so it shows through walls) | `load_overlay::draw` |
| src/load_shape.h | pure geometry for the load overlay (unit tested): stack placement, box corners, screen hull | `load_shape::stack`, `corners`, `hull` |
| src/anim_sync.cpp | ANIM_STATE (0x010A, timestamped): encodes the local player's changed animation variables (30 Hz) with a snapshot every second | `anim_sync::onFrame`, `tick` (logs bytes/s, pulses and render delay every 5 s) |
| src/vehicle_sync.cpp | the first to claim a vehicle in the authority table drives it (claim on entry, release on leaving) | `vehicle_sync::onFrame`, `tick`, `place`, `installEarly`, `isDriving` |
| src/vehicle_wire.h | VEHICLE_STATE (0x0108, unreliable, 64 bytes: seq, role (driver / passenger: the second one in, or the local player riding the partner's vehicle), id, position, rotation rows) | `vehicle_wire::VehicleState`, `decode`, `kRoleDriver`, `kRolePassenger` |
| src/vehicle_load.cpp | the driver owns the vehicle's load: VEHICLE_LOAD (0x0109, reliable: vehicle id, count, kinds) on change and every 5 s while driving | `vehicle_load::onFrame`, `tick` |
| src/fact_wire.h | FACT_SET (0x010B, host to all, reliable) payload: header {count}, entries {kind bool/int, flags, 16-byte fact UUID, value}, `encode`/`decode` with validation | `fact_wire::Entry`, `encode`, `decode` |
| src/fact_sync.cpp | host: sends the queued fact changes each tick, and a FACT_SNAPSHOT of every changed fact to each new peer and each peer that asks (RESYNC) | `fact_sync::onFrame`, `tick` |
| src/equip_sync.cpp | EQUIP_STATE (0x010C, reliable, on change) | `equip_sync::onFrame`, `tick`; a worn-gear slot is only added to when empty; the body's slots are logged before a change and when they match; pieces the body held when the world was saved (marked, loose after the load) are deleted once per gameplay, after the world settled; nothing is created while the local player's own slots are empty (a load starting); runs as a sim_tick callback (engine reads off the net thread), keeps a ledger of the handles it created for the body and deletes only those, leaves the body alone until its slots stop changing, and sweeps the marked orphans of a load once the world is built and before the body is |
| src/rack_sync.cpp | the partner's CARGO_LIST is mirrored into the main load of the remote body's own backpack owner, pieces told apart by kind and order id | `rack_sync::tick` |
| src/env_wire.h | WORLD_ENV (0x010D, host to all, reliable) payload: flags, forecast slot, time of day, day, forecast clock and threshold, 64 region weather types (82 bytes); `decode`, `hoursApart` | `env_wire::WorldEnv`, `decode` |
| src/bt_wire.h | BT_ENV (0x0128, host to all, reliable: u64 BT-active region set) and CATCHER_EVENT | `bt_wire::BtEnv`, `CatcherEvent`, `decode` |
| src/bt_sync.cpp | host: reads the BT regions every 200 ms and sends BT_ENV once a second or at once on a change, sends each catcher activation | `bt_sync::onFrame`, `tick` |
| src/ds2/bt_events.cpp, bt_events.h | BT regions and catchers: detours on SetBtActiveRegion 0x141f09920, the catcher territory activation 0x1418e1300 and a locator's tar 0x141a0b980 | `bt_events::installEarly`, `setRole`, `readActiveRegions`, `takeCatcherEvents`, `followRegions`, `replayCatcher` |
| src/crater_wire.h | CRATER_SET (0x0137, host to all, reliable) payload: u16 count (at most 64) + reserved, then 48-byte craters (locator UUID, position, radius, area, kind) | `crater_wire::Crater`, `encode`, `decode` |
| src/crater_sync.cpp | host: sends the crater set (read by `ds2/craters`) on a change, every 5 s and at once to a joining peer; guest: hands CRATER_SET to `craters::follow` | `crater_sync::onFrame`, `tick` |
| src/ds2/craters.cpp, craters.h | voidout craters of the DSEnvironmentManager (global 0x14623FA08): host reads the committed and pending arrays every 500 ms under tried locks | `craters::installEarly`, `setRole`, `hostSet`, `follow`, `logAll`, `add`, ... |
| src/locker_wire.h | LOCKER_LIST / LOCKER_MOVE / LOCKER_RESULT (0x0134 to 0x0136, reliable) | `locker_wire::LockerHeader`, `LockerMove`, `LockerResult`, `Reason` |
| src/locker_table.h | pure bookkeeping of the shared locker: the guest's moves waiting for the host's answer | `locker_table::PendingMoves`, `MoveLimiter` |
| src/cargo_diff.h | pure comparison of two multisets of cargo pieces by kind and order id: `extra` (handles held beyond the wanted count), `missing` (shortfall per key), `canonicalOrder` / `sameContents` | `cargo_diff::extra`, `missing`, `countsOf`, `keyOf` |
| src/locker_sync.cpp, locker_sync.h | the shared locker on the net thread. Host | `locker_sync::onFrame`, `tick` |
| src/ds2/lockers.cpp, lockers.h | the shared locker, engine side (simulation tick) | `lockers::installEarly`, `setRole`, `takeChanged`, `snapshot`, `queueMove`, `takeReplies`, ... |
| src/ping_wire.h | PING_SET (0x013A, each player to all, reliable) payload: u16 count (at most 100) + reserved, then 24-byte pings (marker id, position, group, index, sequence) | `ping_wire::Ping`, `encode`, `decode` |
| src/ping_sync.cpp, ping_sync.h | pings on the net thread: the local player's changed set (from `ds2/user_markers`) to all, the current set at once to a joining peer | `ping_sync::onFrame`, `tick`, `partnerPings` |
| src/ds2/user_markers.cpp, user_markers.h | the local player's user markers (map menu Add User Marker, in-world marker) | `user_markers::installEarly`, `takeChangedSet`, `currentSet` |
| src/ds2/checked_hook.cpp, checked_hook.h | a detour that is only installed when the function still starts with the bytes the adapter was written for; a mismatch logs a line and leaves the feature off | `checked_hook::install` |
| src/ds2/bt_diag.cpp, bt_diag.h | `diagnostics=1`, host, log only: hunter cling event 0x141abf1d0, and the start / end of the annihilation with the player index it targets (0 Sam, 1 remote body) | `bt_diag::installEarly`, `annihilation`, `ownerText` |
| src/ds2/bt_grab.cpp, bt_grab.h | guest-side vetoes of the BT grab (adapter.ini enemy_sync=1), each hook installed only when the function starts with the expected bytes (`checked_hook`) | `bt_grab::installEarly`, `setRole` |
| src/env_sync.cpp | host: reads the clock and weather every 200 ms and sends WORLD_ENV once a second or at once on a change; guest: hands the newest one to the game layer; releases when it stops being a guest | `env_sync::onFrame`, `tick` |
| src/ds2/world_env.cpp | the game's clock and weather: `game::readWorldEnv` (GameWorldTimeState and DSWeatherManager), `followWorldEnv` / `releaseWorldEnv` | `world_env::installEarly`, `game::readWorldEnv`, `followWorldEnv`, `releaseWorldEnv` |
| src/ds2/enemy_spawn.cpp | detour on EntitySpawnInfo::CreateEntity 0x14016d350 (adapter.ini enemy_sync=1): classifies every spawn by the entity resource's component types; host: reports each enemy to enemy_host | `enemy_spawn::installEarly`, `game::tameEnemies` |
| src/ds2/enemy_host.cpp | host: the tracked enemies (net ids, entity pointers checked by UUID), announced once, pose sampled every 100 ms and sent on change, a 5 s log of how many enemies near the partner are asleep | `enemy_host::add`, `release`, `game::shareEnemies`, `takeEnemySpawns`, `takeEnemyStates`, `takeEnemyGone`, ... |
| src/ds2/test_commands.cpp | `test_commands=1`: command files in the coop folder (tp, area, addweapon, bt) run on the simulation tick | `test_commands::installEarly`, `kCommands`; `loose.txt "r [x]"` logs (x: deletes the gear kinds of) loose pieces near Sam, `body.txt` logs the body's slot pieces |
| src/ds2/enemy_host.cpp | host: the tracked enemies (net ids, entity pointers checked by UUID), announced once, pose sampled every 100 ms and sent on change, death and disappearance reported; a guest role releases them | `enemy_host::add`, `release`, `game::shareEnemies`, `takeEnemySpawns`, `takeEnemyStates`, `takeEnemyGone`, ... |
| src/ds2/partner_focus.cpp, partner_focus.h | (diagnostics=1: sleeper tables, stream test log) the partner's body as a second simulation focus: detour on the per-category entity activity update 0x140171d90 | `partner_focus::installEarly` |
| src/ds2/sneaking_focus.cpp, sneaking_focus.h | the same two-focus idea for the camps' stealth NPCs | `sneaking_focus::installEarly`, `nearestSquaredDistance`, `setSkipWakeBlock` |
| src/ds2/sneaking_diag.cpp, sneaking_diag.h, sneaking_groups.h | diagnostics=1: hooks the NPC managers' update and logs every 5 s each group's members, sleepers, radii, limit and budget, and the members within 80 m of the partner | `sneaking_diag::logGroups` |
| src/ds2/queue_diag.cpp/.h | diagnostic for the intermittent null-entity fault at 0x140136E9C | `queue_diag::installEarly` |
| src/ds2/far_partner.h | the distance (700 m) beyond which a partner is outside the world the host has loaded; marker_overlay shows "too far" | `kBeyondLoadedWorldMetres` |
| src/ds2/test_commands.cpp | `test_commands=1`: command files in the coop folder | `test_commands::installEarly`; `loose.txt "r [x]"` logs (x: deletes the gear kinds of) loose pieces near Sam, `body.txt` logs the body's slot pieces |
| src/ds2/cutscene_log.cpp, cutscene_log.h | `cutscene_log=1`: logs each shared Sequence start | `cutscene_log::enable`, `onStart` |
| src/ds2/cutscene.cpp, cutscene.h | synced cutscenes (adapter.ini `cutscene_sync=1`; test only `cutscene_share_min_frames=N` with test_commands shares every Sequence of N or more frames) | `cutscene::installEarly`, `game::setCutsceneRole`, `takeCutsceneStarts`, `releaseCutscene`, `takeCutsceneEnds`, `armCutscene`, ... |
| src/ds2/sequence_info.cpp, sequence_info.h | reads an engine Sequence: resource (the engine's ref walk, vtable check), category, game state, stop frame, frame, started, loaded networks, stop recorded on the root, entity UUID | `sequence_info::read`, `isSequence`, `started`, `frame`, `stopRecorded`, `startNetwork` |
| src/cutscene_wire.h | CUTSCENE_START / READY / GO / END (0x012A-0x012D) payloads, `decode` with validation, `endedEarly`, the shared categories | `cutscene_wire::Start`, `Ready`, `Go`, `End`, `decode` |
| src/cutscene_table.h | the playbacks and the rules that hold or let through the engine's Sequence start (host, guest, release, go, hold limit); no engine access, tested | `cutscene_table::Table`, `hostDecide`, `guestDecide`, `release`, `go`, `arm` |
| src/cutscene_gate.h | the host's wait for READY from every peer, 5 s timeout | `cutscene_gate::ReadyGate` |
| src/cutscene_sync.cpp | net side: the host sends START and, once the gate opens, GO (its own copy released after half the slowest RTT) and END; a guest answers READY and applies START / GO / END | `cutscene_sync::onFrame`, `tick` |
| tests/cutscene_test.cpp | cutscene wire payload checks and the ready gate |  |
| src/ds2/cargo_defer.cpp | queues deletions of the remote body's cargo pieces, 3 per simulation frame, so a rack change never removes many pieces mid-update | `game::removeCargoLater` |
| src/ds2/loading_screen.cpp | whether the loading screen is up: hooks the constructor 0x1414c0bd0 and scalar destructor 0x1414c0c70 of DSUILoadingScreenMenuFunction, which exists exactly while the screen is shown | `loading_screen::installEarly`, `shown`, `goneFor` |
| src/ds2/world_pause.cpp | `menuUp` (the pause menu is open: DSGameState bit 2) and a sim-tick log line `pause menu open` / `closed` | `world_pause::installEarly`, `menuUp`, `game::keepWorldRunning` |
| src/ds2/game_state.h | the DSGameState bit words (global 0x14623E4C0, low +0x160, high +0x168) read safely; used by world_pause, warp and cutscene_log | `game_state::read` |
| src/ds2/warp.cpp | F6 warp to the partner: queued from the render thread, carried out on the simulation tick through the engine's teleport; refused when dead, riding, in a cutscene, menu or loading; waits while falling | `warp::installEarly`, `warp::poll` |
| src/ds2/camp_alert.cpp | enemy camps' alert phase: the host polls the camp manager every 500 ms and reports changes; a guest sets the phase on its own camp (force alert for phase 4) | `camp_alert::installEarly`, `alertAllCamps` (test command), `game::shareCamps`, `takeCampPhases`, `applyCampPhases` |
| src/camp_sync.cpp, src/camp_wire.h | CAMP_ALERT (0x011F, host to all): camp locator UUID + phase, changes at once and the table every 5 s | `camp_sync::onFrame`, `tick`, `camp_wire::encode`, `decode` |
| src/ds2/enemy_vitals.cpp | an enemy's health: read as 0-254 through the entity's GetHealth / GetMaxHealth slots, written into the damage component's life block on the guest | `enemy_vitals::installEarly`, `readHealth`, `healthAddress`, `applyHealth` |
| src/ds2/enemy_puppet.cpp | guest: tamed enemies by UUID; the host's announcement binds one, its states place it every tick, its death kills it, its disappearance removes it; every puppet is checked against the entity map first | `enemy_puppet::adopt`, `adoptExisting`, `releaseToHost`, `animate`, `game::puppetSpawn`, `puppetStates`, ... |
| src/ds2/entity_lookup.cpp, src/ds2/enemy_pose.h | whether the engine still holds an entity by UUID (EntityManagerGame's map); an entity transform as enemy_wire::Pose | `ds2::entityExists`, `enemy_pose::toWire`, `fromWire` |
| src/enemy_sync.cpp, src/enemy_wire.h | ENEMY_SPAWN / ENEMY_STATE / ENEMY_GONE / ENEMY_ANIM (0x011B-0x011E; ENEMY_ANIM: the animation variables of enemies within 50 m of the partner's body): the host sends, the guest applies | `enemy_sync::onFrame`, `tick`, `enemy_wire::decodeOne`, `encodeStates`, `decodeStates`, `encodeAnim`, ... |
| src/struct_wire.h | STRUCT_REQUEST (0x012A, guest to host, same payload with id kAssignId) and STRUCT_CREATE (0x010F: kind, sub-kind, level, id, guid, transform, durability, the kind's tail) and STRUCT_REMOVE | `struct_wire::Create`, `Remove`, `Placed`, `encode`, `decode` |
| src/peer_joins.h | `PeerJoins`: which peer joins a host has answered, keyed by slot and the net client's PEER_UP serial | `PeerJoins::takeNew`, `clear` |
| src/struct_sync.cpp | host: sends the placed and removed structures each tick and builds a guest's STRUCT_REQUEST; guest: sends its own refused placements and collapses to the host (the host removes and announces) | `struct_sync::onFrame`, `tick` |
| src/weapon_wire.h | WEAPON_STATE (0x0124, reliable, on change: drawn weapon id and hand, id 0 = holstered) and WEAPON_FIRE | `weapon_wire::WeaponState`, `WeaponFire`, `decode` |
| src/weapon_sync.cpp | net thread of weapon sync (adapter.ini `weapon_sync`, on by default) | `weapon_sync::onFrame`, `tick`, `setEnabled` |
| src/ds2/weapon_layout.cpp/.h | the engine's weapon layout in named constants (weapon table of the player entity, table entry, weapon entity, behavior component, the seven CreateAttackRequest functions, vtable slot 46) | `ds2::weapon::weaponId`, `weaponOwner`, `shotBehavior`, `kShotFunctions` |
| src/ds2/local_weapon.cpp | the local side: `game::localWeaponState` reads the current entry of Sam's weapon table | `local_weapon::installEarly`, `game::localWeaponState`, `takeLocalFires` |
| src/ds2/remote_weapon.cpp | the remote side: gives the partner's body its drawn weapon through the engine's own table creation 0x140eab2f0 on a free entry of the body's real weapon table | `remote_weapon::installEarly`, `game::setPartnerWeapon`, `partnerFire` |
| src/ds2/damage_veto.cpp | detour on the damage queue 0x140129de0: damage whose instigator is the partner's body (or its child, the weapon) is not queued unless the combat sync applies it (`remote_apply`) | `damage_veto::installEarly` |
| src/story_wire.h | STORY_EVENT (0x0119, host to all, reliable; a guest sends OrderRequest and OrderDelivered to the host, which completes the mission) | `story_wire::Event`, `decode` |
| src/story_replay.h | the guest's rule for a host mission event: a start replays only for a mission not started here, success/fail only for one in progress (tested in story_wire_test) | `applies` |
| src/story_ledger.h | host ledger of started missions with their real start arguments and section; a joining or resyncing guest gets those starts replayed (no-row fallback for a mission from a save) | `Ledger::startFor` |
| src/story_sync.cpp | host: sends the story events each tick and starts a mission snapshot for a joined or resyncing peer | `story_sync::onFrame`, `tick` |
| src/order_handoff.cpp | host: a guest's accepted order (story.cpp replays its OrderRequest and calls `expect`) is watched by a sim-tick scan | `order_handoff::installEarly`, `expect`, `tick` |
| src/ds2/orders_diag.cpp | log-only instruments for the partner-cargo checks, on only with adapter.ini `orders_diagnostics=1` | `orders_diag::installEarly`, `noteHandOverGather`, `noteCarriedSet` |
| src/ds2/partner_cargo.cpp | the carried-by-player set 0x1411d7530 also walks the remote's owner (delivery and order checks count the partner's rack) | `partner_cargo::installEarly(diagnostics)`, `game::takePartnerMoves`, `baggageOwner` |
| src/partner_cargo_sync.cpp, src/partner_cargo_wire.h | CARGO_GONE (0x011A, to the partner, either direction): what the partner's rack lost to a terminal or to this player, the partner deletes its copy | `partner_cargo_sync::onFrame`, `tick`, `expectAdded`, `acknowledge`, `recentlyLeft`, `recentlyGiven` |
| src/ds2/story.cpp | the host polls the mission map for state changes (sim tick, 500 ms; the baseline restarts at each gameplay epoch, so a load is not reported) and detours the section request and RequestChangeArea | `story::installEarly`, `game::setStoryRole`, `requestStorySnapshot`, `takeStoryRequests`, `forwardOrderRequest`, `missionState`, ... |
| src/ds2/structures.cpp | hooks the structure submit 0x141263280 (player placements by caller 0x14200cc0d) and RequestRemove of a ladder | `structures::installEarly`, `game::setStructureRole`, `takePlacedStructures`, `takeRemovedStructures`, `buildStructure`, `removeStructure` |
| src/ds2/sim_tick.cpp | the one detour on the engine's per-frame object update 0x140215460 (`frame()` is its counter) | `sim_tick::installEarly`, `add`, `Gate`, `gameplayEpoch`, `inWorld`, `worldBuilt`, ... |
| src/time_us.h | `TimeUs` and `nowUs()`: the monotonic microsecond clock every network timestamp uses | `nowUs` |
| src/authority_wire.h | AUTH_CLAIM / AUTH_DECLINE / AUTH_STOP (0x0111-0x0113, reliable, to all) payload `AuthMessage {id, epoch, owner}` and its decode | `authority_wire::AuthMessage`, `decode` |
| src/authority.cpp | the authority table `{object id, owner slot, epoch}` (pure logic): claim, assign (host), release, stop (host), decline | `authority::AuthorityTable`, `onMessage`, `pickOwner`, `claimsOf`, `dropSlot` |
| src/authority_sync.cpp | the one table of the adapter on the net thread | `authority_sync::claim`, `release`, `decline`, `isOwner`, `ownerOf`, `onFrame`, ... |
| src/clock_offset.h | peer clock offset from ping/pong samples (pure): median of the last 16 offsets, valid from 3 samples, peer time to local time | `ClockOffset::addSample`, `toLocal` |
| src/clock_sync.cpp | CLOCK_PING / CLOCK_PONG (0x0114 / 0x0115, unreliable, to one slot): pings every peer (250 ms until 16 samples, then 2 s), answers pings at once, keeps one `ClockOffset` per peer | `clock_sync::toLocalUs`, `rttUs`, `onFrame`, `tick` |
| src/adaptive_delay.h | how far behind real time a stream is rendered (pure): 150-300 ms, grows at once on a late report, shrinks 10 ms per 3 s, lateness measured against the fastest recent transit | `AdaptiveDelay::onArrival`, `delayUs` |
| src/anim_change.h | one animation variable (`Change`: index, type, value) and `valueBytes`, shared by the codec and `ds2/remote_animation` | `remote_animation::Change` |
| src/anim_wire.h | ANIM_STATE (0x010A) and ANIM_EVENT (0x0116) payload: AnimHeader, optional u64 sender time (flag 2), entries; old untimestamped payloads still decode | `anim_wire::encode`, `decode` |
| src/pulse_detector.h | finds one-shot edges (a boolean that leaves its resting value and returns within 0.5 s) in the reported variables (pure) | `PulseDetector::onBool` |
| src/anim_event.cpp | ANIM_EVENT receiver: holds each received pulse 100 ms; `ds2/remote_animation` writes the active ones on top of the partner's state | `anim_event::onFrame`, `active` |
| src/fact_snapshot.h | FACT_SNAPSHOT (0x0117, host to a peer, reliable): chunk header {snapshot id, index, count} plus a FACT_SET payload, up to 256 facts per chunk; receive-side `Progress` | `fact_snapshot::encode`, `decode`, `Progress` |
| src/reject_counters.cpp | per message type and reason (stale epoch, unknown object, wrong sender, invalid owner, malformed, expired, overflow) counts, one `rejects:` log line every 5 s while they change | `reject_counters::count`, `total`, `summaryIfDue` |
| src/resync.cpp | RESYNC (0x0118, reliable): "send me your full state" with scopes facts / authority / anim; modules ask `takeRequests(scope)` from their tick; `request` is inline in resync.h | `resync::request`, `requestLocal`, `onFrame`, `takeRequests` |
| src/resync_trigger.cpp | the manual resync: F9 in the game window, or creating `<game>\coop\resync_now.txt` (deleted when acted on), asks every peer for its state and resends mine, with a toast | `resync_trigger::poll` |
| src/combat_wire.h | ENEMY_HIT (0x0120) / PLAYER_HIT (0x0121) payloads: `EnemyRef`, `HitFields` (the plain DamageParams fields), `decode` with validation (amount, part, finite vectors, named enemy) | `combat_wire::EnemyHit`, `PlayerHit`, `decode`, `validHit` |
| src/combat_rules.h | pure combat rules: per-second hit limiter (per enemy and total) and the once-only death ledger | `combat_rules::HitLimiter::allow`, `DeathLedger::markFirst` |
| src/enemy_directory.h, .cpp | net id <-> entity UUID of the enemies, thread-safe, no entity pointers; the host's enemy module must `set` / `forget`, the guest's combat module fills it from ENEMY_SPAWN | `enemy_directory::set`, `forget`, `netIdOf`, `all` |
| src/enemy_combat.cpp | net side of combat: host validates (payload, rate limit, authority, enemy alive) and queues guest hits, guest queues the host's player hits and deaths; sends what the engine layer collected | `enemy_combat::onFrame`, `tick` |
| src/ds2/damage_diag.cpp | log-only damage instruments, on only with adapter.ini `diagnostics=1` | `damage_diag::installEarly`, `watchSleep` |
| src/ds2/damage_params.cpp | reads the attack context behind a DamageParams (params +0x20 weak ref to the attack link, link +0x38 to the context, context +0x70 the attacker, data block +0x120) | `ds2::damage::contextOf`, `attackerOf` |
| src/ds2/combat_log.cpp | the damage log: one `combat_hit:` line per damage that touches a directory enemy or a player body | `combat_log::before`, `combat_log::after` |
| src/ds2/health_watch.cpp | in-process hardware write watch (Dr0 on every thread through a helper thread, vectored handler recording the writing instruction and stack returns, logged on the sim tick) | `health_watch::arm`, `disarm`, `installEarly` |
| src/ds2/combat_hook.cpp | the hook on ApplyDamage 0x1406fd570 (guest: puppet hits diverted; host: hits on the remote body diverted), building received hits the way script damage does | `setGodMode`, `combat_hook::installEarly`, `game::setCombatRole`, `takeEnemyHits`, `takePlayerHits`, `applyEnemyHit`, ... |
| src/ds2/entity_lookup.cpp | the engine's entity map: existence, `entityByUuid` (GetEntityByUUID 0x140178320), UUID and dead flag of an entity, the EntityManagerGame | `ds2::entityExists`, `entityByUuid`, `entityUuid`, `entityIsDead`, `entityManager` |
| src/ds2/entity_wake.cpp | waking a sleeping entity: `ds2::entityAsleep` (flags +0x98 bit 9) and `ds2::wakeEntity` (Entity wake 0x1401312b0, a wake request the engine serves on a later update) | `ds2::entityAsleep`, `wakeEntity` |
| src/remote_apply.h | the one shared "applying a partner's state" flag (per thread, nesting scopes); every hook of the adapter's own checks `active()` first so applied state is never reported back | `remote_apply::Scope`, `active` |
| src/cargo_menu.cpp | the give/take menu of either player (F7, arrows, Enter): both backpacks side by side, an arrow for the move; claims its keys from the game while open; the lists are copied only while it is open | `cargo_menu::draw`, `claimsKey`, `isOpen` |
| src/marker_overlay.cpp | the draw callback: toasts, each peer's load (load_overlay), a labelled marker | `marker_overlay::draw`, `setSelfMarker` |
| src/peer_pose.cpp | the partner's smoothed position: each sim tick moves each peer's position toward its extrapolated position (snapped on jumps) and hands it to remote_body::setTarget; the overlay reads it | `peer_pose::installEarly`, `shown` |
| README.md | settings and how to remove the mod |  |
| tests/world_to_screen_test.cpp | projection cases (centre, offsets, behind, large coordinates, edge arrow cases) |
| tests/build_guard_test.cpp | identity read from a PE header copy, supported/other stamp/other size/truncated/non-PE |
| tests/msvc_rtti_test.cpp | a class's vtable found by name in the test's own RTTI, the second lookup kept, an unknown class gives 0 (no game) |
| tests/partner_status_test.cpp | status word round trip, health percent, state texts, died / back transitions, away |
| tests/gear_snapshot_test.cpp | multiset difference, format/parse round trip, refused files |
| tests/load_shape_test.cpp | load stack placement and facing, box size, hull |  |
| tests/fact_wire_test.cpp | FACT_SET round trip, truncated, stray byte, unknown kind, entry limit (no game); `--encode/--decode <file>` modes for the python cross-check |  |
| tests/bt_wire_test.cpp | BT_ENV / CATCHER_EVENT round trip, short and long payloads, unknown kind and flags, region set arithmetic (no game) |  |
| tests/ping_wire_test.cpp | PING_SET round trip (0, 1, 100 pings), sizes, count 101, NaN / infinite position, beyond 100 km, group 5, index 20, sequence 0, reserved bytes, duplicate ids, ordering, the fake launcher's bytes |  |
| tests/locker_wire_test.cpp | LOCKER_LIST round trip (0, 1, 128 pieces), 129 pieces, wrong sizes, kinds 0 and 27, flags bit 1, snapshot end with a key / piece / kind, NaN durability, reserved fields |  |
| tests/cargo_diff_test.cpp | plain duplicates (3 held, 1 wanted), order pieces told apart by order id, extra and missing together, an empty want empties, canonical order and `sameContents` (no game) |  |
| tests/locker_table_test.cpp | request ids from 1, a pending locker per key and kind, result clears and a repeat is ignored, the 10 s timeout counted, 20 moves per second per guest (no game) |  |
| tests/crater_wire_test.cpp | CRATER_SET round trip (0, 1, 64 craters), wrong size, count 65, NaN / infinite position, radius 0 and over the maximum, kind 16, reserved fields, ordering (no game) |  |
| tests/env_wire_test.cpp | WORLD_ENV round trip, wrong size, time outside the day, time-of-day distance (no game) |  |
| tests/peer_joins_test.cpp | new slot, same peer, a peer replacing one on its slot, leave and rejoin, clear (no game) |  |
| tests/struct_wire_test.cpp | STRUCT_CREATE / STRUCT_REMOVE round trip, short and long payloads, unsupported kind, wrong tail (no game) |  |
| tests/enemy_wire_test.cpp | ENEMY_* round trips, truncated and oversized batches (no game) |  |
| tests/vehicle_wire_test.cpp | VEHICLE_STATE layout, driver and passenger round trips, unknown role, short and long payloads, message id (no game) |  |
| tests/weapon_wire_test.cpp | WEAPON_STATE / WEAPON_FIRE round trips, short and long payloads, unknown hand and kind, pellet limit, non-finite floats, message ids (no game) |  |
| tests/story_wire_test.cpp | STORY_EVENT round trip, short/long payload, unknown kind (no game) |  |
| tests/cargo_give_wire_test.cpp | CARGO_GIVE id 0x0133, CargoAdd decode (order link survives, short/long/empty payload refused) (no game) |  |
| tools/ds2/ds2types.py | queries DS2's reflected type schema from G:\coop-scratch\ds2	ypes\ds2_types.json (not in the repo): `<Class>`, `find <re>`, `field <re>`, `at <Name> <hex>` |  |
| docs/research/ (MULTIPLAYER_MODS.md, _2, _DEEP) | survey of 15 co-op mods and ranked ideas for DS2 (ideas only, no code copied) |  |
| tests/authority_test.cpp | claim / release / decline / stop across three tables, stale and duplicate messages, the tie rule, barred owners, wrong senders, leaving slots, the change handler, reject counters (no game) |  |
| tests/combat_wire_test.cpp | combat payload round trips, hit validation bounds, hit rate limit windows, once-only deaths, the enemy directory (no game) |  |
| tests/resync_test.cpp | RESYNC requests per slot and scope, handed out once, malformed counted; the shared applying flag (no game) |  |
| tests/timing_test.cpp | ClockOffset and AdaptiveDelay (no game) |  |
| tests/anim_wire_test.cpp | ANIM_STATE / ANIM_EVENT payload with and without timestamp, rejections, pulse detection, held pulses (no game) |  |
| tests/fact_snapshot_test.cpp | FACT_SNAPSHOT chunking and header checks, progress (no game) |  |
| tools/ds2/fact_wire_test.py | cross-language FACT_SET check: python encodes and C++ decodes, C++ encodes and python decodes, truncation rejected (no game) |  |
| tools/ds2/savefmt.py | DS2 save container on copies: 32-byte header, XOR key from a Murmur hash of the header seed, index / size table / chunks (chunk 0 text, chunk 1 PNG) | `segments`, `assemble`, `key_for`, `lz4_block` |
| tools/ds2/savefmt_test.py | decrypt then re-encrypt equals the save, PNG in chunk 1 (on the copies in G:/coop-scratch/ds2/savefmt or given files) |  |
| tools/ds2/pe_image.py | read-only view of a PE file laid out by RVA, for static scans of Decima executables (pointers in initialized data are absolute VAs at the preferred image base) | `PeImage` |
| tools/ds2/gear_pair.py | live: copies the session saves, gives Sam a weapon (test command), presses F at a terminal (autosave), copies again and lists the changed saves, for the gear-only diff |  |
| tools/ds2/menu_path.py | sends a comma list of gamectl keys to the game with a screenshot after each (System menu to Load to a save) |  |
| tools/ds2/session_fix.py | makes Continue load a chosen save from the coop session folder | `build`, `rewrite_time` |
| tools/ds2/qol_verify.py | live re-check of the qol features: build guard line, health label, edge arrow, death toast shots, F9 resync log |  |
| tests/proxy_load_test.cpp | loads the built version.dll and calls a forwarded export |  |
| tests/overlay_test.cpp | real D3D12 swap chain created after the hooks: every Present draws, also after the swap chain is recreated on a new queue and format |  |
