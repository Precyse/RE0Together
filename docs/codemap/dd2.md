# CODEMAP: dd2

Dragon's Dogma 2: the REFramework C++ plugin. Built: phase 1 of the design (`coop-scratch\dd2\design\phases.md`), the
world gate, the owner half of the body streams, the shared world clock, forced offline play, the look card, peer puppets,
the guest's save redirect, the co-op profile (capture in solo play, splice at a co-op load) and the enemy sync (host enemies mirrored on the guest, guest hits replayed on the host) (built, not live-tested). Index and "where to look for X": `CODEMAP.md`.

## adapters/dd2/ (C++20, x64, `reframework\plugins\dd2coop.dll`)

Build (VsDevCmd `-arch=amd64`): `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build`; the DLL is
copied to `adapters/dd2/dd2coop.dll`. Unit tests: `ctest --test-dir build`. Needs REFramework (dinput8.dll) in the game
folder; uses the launcher link, logging and crash dumps of `adapters/re0/src` and `adapters/common`.

| file | owns | key members |
|---|---|---|
| src/plugin_entry.cpp | the two REFramework exports; start-up order: crash dumps, config, build gate, hook sets, calls, save redirect and profile (splice for a co-op guest, capture otherwise), session, tick; names hash | `reframework_plugin_required_version`, `reframework_plugin_initialize` |
| src/reframework/API.h | REFramework's C plugin API 1.10.0 (vendored, MIT) | |
| src/refw.cpp | the API as the plugin uses it: method by type, name and parameter types; body rebased to 0x140000000; enum literals; hooks; invoke; managed and native singletons; runtime type names; parameter names; static fields; strings; TDB hash | `refw::findMethod`, `rebasedBody`, `enumLiterals`, `addHook`, `removeHook`, `invoke`, `managedSingleton`, `paramNames`, `staticField`, `typeOf`, `createInstance`, `writeRef`, `field`, `managedString`, `stringValue` |
| src/names.h | every hooked method (sets world_gate, puppet, profile, plague, roster and mirror): type, name, parameters, pinned rebased body, dump share count, set; shared bodies; enum types | `names::kAll`, `kSharedBodies` |
| src/hook_rule.h | rules H1 (pinned body) and H2 (no shared body) (pure, unit tested) | `hook_rule::decide` |
| src/hook_registry.cpp | installs a set all or nothing (rolls back on a refusal), one line per set | `hook_registry::installSet` |
| src/build_gate.cpp | DD2.exe PE TimeDateStamp 0x6ae0ce34; DD2gether present = inert | `build_gate::checkRunningGame`, `timeDateStampOf` |
| src/config.cpp | `coop\adapter.ini`: port 27985, coop, role, echo_puppet, peer_puppet, enemy_sync, campaign, arisen | `loadConfig` |
| src/world_gate_rule.h | place (title, loading, in game, cutscene, down, travel, rift) and the load serial from flow phases, scene types and ferrystones (pure, unit tested) | `world_gate_rule::Gate` |
| src/world_gate.cpp | the world_gate set: MainFlowManager.changeFlowPhase, changeSceneType, onCompleteSceneLoad, FerrystoneFlowController.startLoad; rule T1 (game thread only) | `world_gate::install`, `place`, `loadSerial`, `setGameThread` |
| src/wire_dd2.h | DD2 wire messages GAME_HELLO, PLACE, BODY_CARD, BODY_STATE, ACTION_EVENT, CLOCK (pure, unit tested) | `wire_dd2::decodeHello`, `decodePlace`, `decodeBodyCard`, `encodeBodyCard`, `encodeCardEntries`, `decodeBodyState`, `decodeActionEvent`, `decodeClock` |
| src/session.cpp | launcher client, GAME_HELLO on every join, PLACE on change + 2 s; peers' reports; routes every other frame of a matching peer to its module | `session::start`, `tick`, `send`, `sendMessage`, `active`, `isHost`, `hostPlace`, `peerPlace`, `matchingPeers` |
| src/calls.h, src/calls.cpp | every method the plugin calls, pinned like names.h; a moved body turns its feature (body, clock, offline, serializer, puppet, saves, profile, enemy) off; puppet and profile also need the serializer, enemy the puppet rows | `calls::kRows`, `resolve`, `enabled`, `method`, `objectResult` |
| src/local_body.cpp | a body through the engine's getters (the local Arisen, the local main pawn, or a puppet): world position, yaw, action hash per layer, HP, death, discrete speed | `local_body::character`, `sample`, `sampleCharacter`, `yawOf` |
| src/owner_stream.cpp | BODY_STATE at 30 Hz (4 Hz held) and ACTION_EVENT on action changes, for the Arisen and the main pawn | `owner_stream::tick`, `lastSample` |
| src/peer_bodies.cpp | each peer's newest body state and pending action edges per body kind on the shared stream clock (`re0/src/stream_clock.h`); summary every 30 s | `peer_bodies::onBodyState`, `onActionEvent`, `latest`, `takeActionEdges`, `forget`, `tick` |
| src/clock_rule.h | when a guest sets its clock to the host's (pure, unit tested) | `clock_rule::decide` |
| src/world_clock.cpp | host sends CLOCK; guest applies through TimeManager.setTime with parameters matched by name | `world_clock::install`, `tick`, `onClock` |
| src/offline.cpp | NetworkManager.requestForceOfflinePlay once per session | `offline::tick` |
| src/serializer.cpp | the engine's via.Serializer JSON: a managed object of a named type to UTF-8 JSON and back | `serializer::toJson`, `fromJson` |
| src/text.h | UTF-16 to UTF-8 and back | `text::utf8`, `utf16` |
| src/save_sections.cpp | the ContextDBMSSaveData and ItemManager.GameSaveData sections: offsets, the Arisen's and main pawn's CharacterIDs, List access, a record's CharacterID, the world-owned context types | `save_sections::init`, `ownerOf`, `records`, `recordCharacter`, `worldOwned` |
| src/profile_rule.h | the profile file `coop\profile\profile.dd2p`: header (tdb hash, level, job, equipment set), one block per piece of JSON (pure, unit tested) | `profile_rule::encode`, `decode` |
| src/profile.cpp | capture: each save's contexts, items and money of the Arisen and main pawn into the profile; splice: a co-op guest's two loaders get the profile's pieces in place of the host's characters, then the level is checked | `profile::install`, `tick` |
| src/roster_rule.h | the enemy roster key (UniqueID row Guid + index) and the host-versus-own comparison (pure, unit tested) | `roster_rule::Key`, `compare` |
| src/roster.cpp | observe stage of phase 7 (probe P7-1): keys of the enemies and monsters the engine registered; the host sends ENEMY_ROSTER once settled 3 s (and to joiners), a guest logs equal / extra / missing; members (key, component, character) and the host's keys for the enemy sync | `roster::install`, `tick`, `onFrame`, `members`, `hostKeys`, `settled` |
| src/enemy_rule.h | the enemy sync's rules: streamed within 120 m of a guest, hit damage bounds, the per-target and per-sender hit budget (pure, unit tested) | `enemy_rule::streamed`, `damageOk`, `HitBudget` |
| src/enemy_spawn.cpp | an enemy's generate record across machines: container to the engine's save record and JSON (host), back to a container and a generate request (guest); removal of our own enemy | `enemy_spawn::init`, `recordOf`, `create`, `destroy` |
| src/enemy_stream.cpp | host half of the enemy sync (on in co-op, enemy_sync=0 off): ENEMY_STATE at 10 Hz, ENEMY_HIT replayed through HitManager.directDamage from the guest's puppet, ENEMY_WANT answered with ENEMY_SPAWN at 4 per second | `enemy_stream::setEnabled`, `tick`, `onHit`, `onWant` |
| src/enemy_mirror.cpp | guest half: our enemies in the host's roster become mirrors (brain off through the puppet gates, driven by the puppet driver, HP set to the host's, killed natively with the host's), extras removed, missing ones asked for and generated; our own bodies' hits on a mirror captured from the HP gate and sent | `enemy_mirror::setEnabled`, `tick`, `isMirrorObject`, `onDamage`, `onState`, `onSpawn` |
| src/object_set.h | the engine object pointers the gates test from any thread (puppets, mirrors) | `ObjectSet::replace`, `contains` |
| src/plague_off.cpp | dragonsplague off while coop=1: possession points and level rises refused, the inn's outbreak check answers none, the outbreak NPC kill does nothing; a 60 s refusals line | `plague_off::install`, `tick` |
| src/save_folders.cpp | where a launch keeps its world and character: a guest coop\session with the picked character (`arisen`), a host's campaign coop\campaigns\<id> (`campaign`), else the game's own save with coop\profile; launcher ids checked | `save_folders::of`, `validId` |
| src/save_redirect.cpp | the SaveService mount set to the launch's world folder (save_folders.h; directory `<mount>\win64_save`), read back and logged | `save_redirect::enable`, `tick`, `verified` |
| src/look.cpp | the look contexts (HumanStatus + six from the ContextHolder), read from any character, to and from the engine's via.Serializer JSON, written into a constructed ContextWriterOnline | `look::kSlots`, `of`, `toJson`, `fromJson`, `release`, `writer` |
| src/puppet_builder.cpp | a character through the stray-pawn route: online product under an unused online CharacterID, the riftstone summon's generate request at a place, removal with GenerateManager.requestDestroy (`destroy`, shared with the enemy sync); one id and product per puppet for the session, a rebuild swaps the writer (recipe: `coop-scratch\dd2\design\static_checks.md`) | `puppet_builder::create`, `rewrite`, `generate`, `despawn`, `bodyGone`, `release` |
| src/echo_puppet.cpp | probe P4-1 (adapter.ini echo_puppet=1): the own Arisen's look through JSON and back, built 3 m ahead | `echo_puppet::tick`, `setEnabled` |
| src/body_card.cpp | BODY_CARD: the look JSON of the own Arisen and main pawn on change (10 s check) and to every newly matching peer; each peer's newest card per body kind | `body_card::tick`, `onFrame`, `peerCard` |
| src/peer_puppet.cpp | each peer's Arisen and main pawn built from their cards at their newest BODY_STATE while both are in plain gameplay (the main pawn seated in our party with hirePawn, dismissed by CharacterID on removal), kept through menus, cutscenes and downs, then driven; rebuilt on a new card or when the game removed the body, removed on leave or load (adapter.ini peer_puppet=1, needs the puppet set) | `peer_puppet::tick`, `setEnabled` |
| src/puppet_registry.cpp | our puppets' ids and, once the engine reports a body ready, its character and controllers; the driver scope the gates let through | `puppet_registry::expect`, `forget`, `body`, `destroyed`, `onReady`, `isPuppetId`, `isPuppetObject`, `DriverScope` |
| src/puppet_brain.cpp | the puppet hook set: ready and destroy binding, gates refusing navigation, action, locomotion and HP requests on a puppet or enemy mirror outside the driver (a mirror's HP change goes to `enemy_mirror::onDamage`), the mirror set's monster overrides, vetoes of stray-pawn leave and destroy and pawn lost for our ids; a per-layer summary | `puppet_brain::install`, `installed`, `installMirror`, `tick` |
| src/puppet_driver.cpp | drives a bound body: warp when over 1.5 m off, else the owner's discrete speed with the turn target on the owner (or along its facing), the owner's action edges by hash | `puppet_driver::init`, `drive` |
| src/tick.cpp | UpdateBehavior / LateUpdateBehavior entries: thread lines, hang watchdog beat, 30 Hz step (save redirect, session, profile, puppets, streams) | `tick::install` |
| tests/rules_test.cpp | hook rows, gate, wire, BODY_CARD, ENEMY_ROSTER and the roster comparison, the enemy messages and rules, profile file, clock rule, yaw | |

## tools/dd2/

| file | owns |
|---|---|
| verify_rows.py | checks every names.h / calls.h row (app, via and System types) against a type database dump (method, parameters, pinned body, share count); run after a game update |
