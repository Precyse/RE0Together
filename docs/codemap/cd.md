# CODEMAP: cd

Crimson Desert: the x64 adapter. Built: phase 1 of the design (`coop-scratch\cd\design\phases.md`), the world gate, the
phase 3 presence recipes (copies of the other player) and the phase 4 pure rules. Index and
"where to look for X": `CODEMAP.md`.

## adapters/cd/ (C++20, x64, winmm.dll proxy in `bin64`)

Build (VsDevCmd `-arch=amd64`): `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build`; the DLL
is copied to `adapters/cd/winmm.dll`. Unit tests: `ctest --test-dir build`. Shares the launcher link, logging, crash dumps,
MinHook wrapper and vendored MinHook with `adapters/re0` (`../re0/src`, `../re0/third_party`) and `adapters/common`.

| file | owns | key members |
|---|---|---|
| src/main.cpp, src/winmm_exports.inc | the 180 winmm.dll exports forwarded to the system DLL; DllMain starts the adapter in CrimsonDesert.exe only (not crashpad_handler, pers) | `DllMain`, `hostIsGame` |
| src/init.cpp | start-up order: crash dumps, config, build gate, wait for WorldSystem (Denuvo has run), catalogue, hook sets in one batch, link | `initThread` |
| src/config.cpp | `bin64\coop\adapter.ini`: port 27990, capture, dev_commands, presence, story_replay | `loadConfig` |
| src/cd_sites.h | the pinned build's hook sites with their 16 expected bytes, by set (funnels, tick); WorldSystem global | `cd_sites::kAll`, `kPostPacket`, `kOnRecvPacket`, `kProcessRecvQueue`, `kWorldSystem` |
| src/build_gate.cpp | PE TimeDateStamp 0x6AEB7D59 and base 0x140000000; site bytes per set (unit tested) | `build_gate::checkRunningGame`, `setIntact`, `siteMatches`, `identityOf` |
| src/safe_read.cpp | SEH-guarded reads of game memory | `safe_read::bytes`, `value`, `pointer` |
| src/troc_table.h | the Troc catalogue as data: name from the RTTI type name, opcode and name lookups, missing required names (pure, unit tested) | `troc_table::Table`, `messageName` |
| src/troc_catalogue.cpp | finds the static pa::TrocTr instances (1,175) by RTTI name in every writable, non-executable section; writes `coop\troc_catalogue.tsv` | `troc_catalogue::build`, `table`, `nameOf`, `kRequired` |
| src/packet.h | the funnel packet header (u16 opcode, u8 slot, u16 payload length) (pure, unit tested) | `packet::parse`, `Direction` |
| src/funnels.cpp | ClientNet::PostPacket and OnRecvPacket hooks: per-opcode counts, capture, events (head or whole payload) for watched opcodes, the S2C injection queue drained inside the real OnRecvPacket (64 per call, a fault turns injection off), our own Reqs posted from inside the real PostPacket with the game's channel and flags for that opcode (3 s wait limit), one held opcode kept back while holding (the real call's result set as queued) and posted when the hold ends | `funnels::install`, `watch`, `inject`, `injectionOn`, `post`, `postingOn`, `holdOpcode`, `setHolding`, `takeEvents`, `totals`, `opcodeCount` |
| src/capture.cpp | `coop\capture-<stamp>.cdcap` writer thread (format in capture.h) | `capture::start`, `record`, `dropped` |
| src/main_tick.cpp | ProcessRecvQueue hook: the game-thread tick (thread line, hang watchdog beat, module order) | `main_tick::install` |
| src/actor_key.h | actor key classes and index (pure, unit tested) | `actor_key::classOf`, `indexOf` |
| src/world_gate_rule.h | Title / Loading / AwaitLocalReady / Ready and the load serial (pure, unit tested) | `world_gate_rule::Gate` |
| src/world_gate.cpp | feeds the gate from the load bracket messages and our own player MoveActorReqs; the CD_WORLD this machine reports | `world_gate::watchOpcodes`, `onEvent`, `tick`, `report` |
| src/wire_cd.h | CD wire messages 0x0300-0x0303 (CD_HELLO, CD_WORLD, CD_MARKER, CD_COUNTERS) and 0x0310-0x0318 (CHAR_TEMPLATE, CHAR_MOVE, CHAR_ACTION, CHAR_ROTATE, CHAR_EQUIP, CHAR_GONE), 0x0370 (FIELD_TIME) and 0x0390 (STAGE_BRANCH), decoders (pure, unit tested) | `wire_cd::decodeHello`, `decodeWorld`, `decodeTemplate`, `decodeStream`, `decodeEquip`, `decodeFieldTime`, `decodeStageBranch`, `encodeCounters` |
| src/presence_rule.h | Move / StartAction / Rotate Req to Ack recipes, RemoveActorAck, packet framing (pure, unit tested) | `compactMove`, `moveAck`, `actionAck`, `rotateAck`, `actionIsSelfContained`, `removeAck`, `packetOf` |
| src/template_rule.h | a player's template (user + body ActorLoadDataAck) turned into a copy: slot key blocks, key rewrite, character number, position, body cut, key barrier, copy key tests (pure, unit tested) | `makeCopy`, `senderKeys`, `truncateBody`, `copyKeys`, `isCopyKey`, `copySlotOfPlayer` |
| src/presence_send.cpp | captures the own template from the load stream, sends CHAR_TEMPLATE after Ready + 2 s and to joiners, the own Move / Action / Rotate Reqs; the own clock anchor; checks this build's Req sizes against the recipes | `presence_send::watchOpcodes`, `onEvent`, `tick`, `ownStampNow`, `recipesMatch`, `generation` |
| src/equip_rule.h | equipment since the template (Loadout), the unequip-then-put plan for a copy, serials, OtherEquipItemAck / OtherUnequipItemAck payloads, UnequipItemAck parse (pure, unit tested) | `Loadout`, `plan`, `nextSerial`, `otherEquip`, `otherUnequip`, `parseUnequipItemAck` |
| src/equipment.cpp | the server's OtherEquipItemAck writer hook and our UnequipItemAck feed the own Loadout, sent as CHAR_EQUIP on change and to joiners; each peer's Loadout replayed on its live copy of the same template generation, again after every respawn | `equipment::resolveOpcodes`, `install`, `onEvent`, `tick`, `onFrame` |
| src/presence_recv.cpp | each peer's copy: spawn (two loads, first action, still Move), streams as Acks on our clock with Move thinning, removal (leave, 8 s silence, new generation); adapter.ini presence=1; a copy's view for the modules that dress it | `presence_recv::onFrame`, `tick`, `setEnabled`, `canInject`, `copyView` |
| src/minimap.cpp | the client's minimap icon decision answers the outlined arrow for copy keys (from the game's icon table, without the original); a map pin at a peer's reported position while its copy is not here | `minimap::resolveOpcodes`, `install`, `tick` |
| src/pin_rule.h | when the pin is created, moved (3 m, at most 1/s), refreshed (10 s) or removed; Create / Remove payloads (pure, unit tested) | `pin_rule::decide`, `createPayload`, `removePayload`, `pinId` |
| src/containment.cpp | character-control update and post-update return at once for a copy whose action layer has a chart but no playback (the worker-thread crash); copies spawn only with it in | `containment::install`, `confirmEnabled`, `active`, `skipped` |
| src/field_time_rule.h | pa::FieldTime: raw time (night at half), memory and SetFieldTimeReq layouts, the host's hour on our nearest day, the catch-up limiter (pure, unit tested) | `FieldTime`, `fromMemory`, `setReq`, `nearestWithTimeOf`, `CatchUp` |
| src/world_time.cpp | the host sends FIELD_TIME every 5 s; a guest more than about two game minutes off posts SetFieldTimeReq to its own server (at most 1 per 15 s, 8 per 5 min); confirmations counted | `world_time::resolveOpcodes`, `onEvent`, `tick`, `onFrame` |
| src/link.cpp | launcher client, CD_HELLO on every join, CD_WORLD on change + 2 s, CD_COUNTERS every 5 s while changing; peers' reports; routes CHAR_* frames of matching peers to presence_recv and equipment, FIELD_TIME to world_time; the host role | `link::start`, `tick`, `send`, `peerWorld`, `peerSlots`, `peerJoinedThisTick`, `isHost`, `hostSlot`, `sendMarker` |
| src/self_hash.cpp | SHA-256 of the installed DLL for CD_HELLO | `self_hash::adapterHash` |
| src/census.cpp | the once-a-minute funnel census line (busiest opcodes by name, counters that must stay 0) | `census::tick` |
| src/dev_cmd.cpp | `coop\cmd.txt` commands (dev_commands=1): `marker` | `dev_cmd::tick` |
| src/stream_clock_rule.h | on top of the shared `stream_clock::StreamClock`: restamp clamp, pause detector, player Move thinning (pure, unit tested; phase 3) | `restamp`, `PauseDetector`, `MoveThinner` |
| src/zone_rule.h | the shared zone around the host (on 150 m / 2 s, off 250 m / 5 s, fight wait, host death grace, epoch) and the golden predicate of shared actors (pure, unit tested; not wired yet: phase 5) | `zone_rule::Zone`, `shared` |
| src/hit_rule.h | the host's acceptance of a guest's HIT_INTENT with a refusal reason (pure, unit tested; phase 6) | `hit_rule::Gate::check`, `Refusal` |
| src/kill_rule.h | one commit per death and the xp_share credit policy for both worlds (pure, unit tested; phase 6) | `kill_rule::Ledger`, `credit` |
| src/save_hold_rule.h | the save hold: reasons, 30 s bound (shadows ended, replays abandoned, released until the reasons clear), a manual save held at most 5 s (pure, unit tested; not wired yet: phase 7) | `save_hold_rule::Hold::tick`, `onManualSave` |
| src/story_rule.h | the shared story log: host log (dense numbers, 4,096 max), guest queues per u32 sequencer key with chaining, the resume rule, retry and expiry, digest (pure, unit tested) | `story_rule::HostLog`, `GuestLog::offer`, `next`, `applied`, `waitingKeys`, `digest` |
| src/stage_packets.h | StageBranchStateReq / Ack, PrepareSequencerAck, DeleteSequencerAck payloads, verified and inferred offsets tagged (pure, unit tested) | `stage_packets::parseReq`, `encodeReq`, `parseAck`, `parsePrepare`, `parseDelete` |
| src/story.cpp | the shared story: own instances and sequencer keys from own traffic (cleared per load); the host logs each branch its server applied and sends STAGE_BRANCH (whole log to joiners); a guest queues them and, with story_replay=1, posts a guarded branch as its own Req and drops it once applied, otherwise logs it; a 30 s line checks the Ack's field offsets live | `story::resolveOpcodes`, `setReplay`, `onEvent`, `tick`, `onFrame` |
| src/save_guard.cpp | a guest with story replay on holds the game's SaveAutoReq while a replay waits or a load runs (30 s bound, then replays abandoned) and posts it after | `save_guard::resolveOpcodes`, `tick`, `stats` |
| src/stage_guard.cpp | hook on the client's stage lookup that records the stage manager; the stage of an instance by memory reads (every bucket); its branch table and whether a branch is in range and readable | `stage_guard::install`, `reset`, `table`, `branchOk` |
| tests/rules_test.cpp | gate, keys, packet header, Troc table, wire, PeerRoster, stream clock, zone, hits, kills, recipes, templates, equipment, pins, copy keys, save hold, story, stage packets, field time | |

## adapters/common/

| file | owns | key members |
|---|---|---|
| peer_roster.h | the session's other players with a per-peer state; a rejoin starts fresh; frames from a slot that left find nothing (pure, tested in cd's rules_test) | `PeerRoster::update`, `find` |

## tools/cd/

| file | owns |
|---|---|
| port_sites.py | ports addresses of one build to the next by masked byte signatures (strict, loose, body after the prologue, call-site votes, call order), sites by the containing function or their own signature, data through their referencing instructions; reads the old export's names.tsv, calls.tsv, data_xrefs.tsv |
| capture_dump.py | decodes a `.cdcap` capture with `troc_catalogue.tsv`: packet lines, `--summary` (sizes per message for size_table.h), `--golden` (one packet per message and size) |
