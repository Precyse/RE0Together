# Dragon's Dogma 2 plugin

`reframework\plugins\dd2coop.dll` in the game folder: a REFramework C++ plugin (API 1.10.0). Phase 1 of the design
(`coop-scratch\dd2\design`): the world gate (load serial and place from MainFlowManager and the ferrystone flow), the
game-thread tick, the session messages, each player's body stream (position, facing, actions), the host's world clock
on the guest, forced offline play during a session, peer puppets, the guest's save redirect and the co-op profile
(phase 2: the guest plays the host's world as their own Arisen).

Supported build: DD2.exe PE TimeDateStamp 0x6ae0ce34 (Steam build 26256634). Another exe, or DD2gether in
`reframework\autorun\coop`, leaves the plugin inert. File map: `docs/codemap/dd2.md`.

## Build
VsDevCmd `-arch=amd64`, then `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build` and
`ctest --test-dir build`. The DLL lands in `adapters/dd2/dd2coop.dll`.

## Install by hand (one PC, no launcher)
REFramework must be installed (`dinput8.dll`). Move DD2gether out of `reframework\autorun` first (the plugin refuses to
run beside it). Copy `dd2coop.dll` to `reframework\plugins\`.

## Phase 1 check (probes P1-1, P1-2)
`coop\adapter.log` must show: `build_gate: DD2.exe stamp 0x6ae0ce34 supported`, `world_gate: N phases, 6 scene types`,
`world_gate: 4 hooked`, `tick: 2 entries registered`, one `tick: UpdateBehavior runs on thread N` and
`LateUpdateBehavior ... same as UpdateBehavior`, then `world_gate: phase ...` lines through the title and one
`load serial N (entered InGame ...)` per load (title load, ferrystone, inn rest, game over).

## Session check (two PCs, DD2gether moved out)
Each log shows `calls: body on`, `calls: clock on`, `calls: offline on`, `world_clock: day ... s, hour ... s, minute ... s;
host side on, guest side on` (off when the clock units were not read), then with a peer:
`offline: forced offline play requested (ok)`, `peer_bodies: slot N: ... states, ... actions ...` every 30 s with a sane
distance, and on the guest `world_clock: ... game s off the host: set day ...` after joining.

## Puppet probe (P4-1, one PC)
`echo_puppet=1` in `coop\adapter.ini` (test only). 10 s into plain gameplay the log shows each context `copied`,
`look as JSON, N bytes` (the look through the engine's serializer and back, as a peer's arrives), `product registered
for online CharacterID 0x...`, then `generate requested ...`; a copy of your own Arisen should appear 3 m in front of
you. Any `stopped at <step>` line names the step that failed. Set it back to 0 afterwards.

## Peer puppets (two PCs; on by default, `peer_puppet=0` in `coop\adapter.ini` turns them off)
In plain gameplay each side logs `body_card: sent (...)` and
`body_card: slot N card 0x...`, then `peer_puppet: slot N built from card ...` and `puppet_registry: body of 0x... ready`:
the other player's Arisen and main pawn appear where they are and follow them (`main pawn seated in the party`, or
`not seated` when the party already has two hired pawns) (warp when far, walk or run toward them, their
actions replayed). `puppet_brain: <layer> refused N times` lines show the game's own AI being kept off it. Leaving or a
loading screen logs `removed`; a gear change logs `removed (new card)` and a rebuild.

## Save-carry (phase 2; one PC first, then two)
1. Solo, with the plugin installed: load your own save and save once (inn or menu). The log shows `profile: captured
   level=.. job=.. pieces=.. gold=..` and `coop\profile\profile.dd2p` exists (probe P2-2 reads the item lines).
2. A co-op session as the guest: the launcher copies the host's `data000.bin` and your own `data00-1.bin` into
   `coop\session\win64_save` and writes `coop=1`, `role=guest`. The log shows `save_paths: before dir=.. mount=..`, then
   `after` with both inside `coop\session` (a `REFUSED` line means stop: the real save is not protected; the launcher's
   backup is in `coop\archive\real`). Probe P2-4: the real save folder's files keep their hashes after the session.
3. Load the host's world: `splice: contexts of 2 characters, N replaced, ...`, `splice: items N removed, N added`, then in
   gameplay `splice: ok level=.. job=..`: your own Arisen, gear, inventory, gold and main pawn in the host's world.
   `splice: MISMATCH` or `refused` counts mean a piece the engine would not take; the log names it.
In co-op (`coop=1`, both roles) dragonsplague is off: `plague: 6 hooked`, then `plague_off: N dragonsplague steps refused`
lines once a pawn would have caught it or an inn check ran.

Enemies (phase 7, probe P7-1): with both in the same area, the guest logs `roster: host N (load S), own N: N equal, N
extra here, N missing here`. Equal covers the spawns both lotteries rolled (the key is the generate row and the spawn's
number in it, static_checks.md); extra and missing are the rolls that differed.

## Enemy sync (on in co-op; `enemy_sync=0` in `coop\adapter.ini` turns it off; with peer puppets on, hits have an attacker)
The host simulates every enemy. Start-up logs `calls: enemy on`, `mirror: 3 hooked` and `enemy_spawn: OverrideContextPosition
literal found`. Near the host, the guest's enemies that the host also has become mirrors that follow the host's (warp
when 1.5 m off, the host's actions replayed, HP set to the host's) and die when the host's die (XP and drops on each side);
its extras fade out, and the host's others are created from their generate records (probe P7-2). Every 30 s the guest
logs `enemy_mirror: N mirrors; ... killed with the host, ... extras removed, ... created, ... hits sent` and the host
`enemy_stream: N guest hits replayed, N refused, N records sent`. Live checks: a guest's sword hit staggers the host's
enemy as a native hit does (P7-5), mirrors do not attack on their own (P7-4), a created enemy appears where the host's is
(`enemy_spawn: stopped at <step>` names a refusal), the mirror's HP bar follows the host's. Only enemies within 120 m of the host's
body are mirrored, removed or created on the guest; farther out the guest's own enemies keep their own AI (no enemies are simulated
away from the host yet). A mirror whose host state is older than 1 s, or any mirror while the host is silent, is released (AI
back on, damage accepted again).

## Join warp and support pawns (co-op; guests warp, both sides seat main pawns)
After each load of ours and of the host's, a guest more than 30 m from the host's body logs `join_warp: N m from the host (load S,
host load S), warped to it` (`no warp` when near, `warp refused` when the call failed), once per load pair. Support pawns (the
two hired party seats): the guest logs `support_pawns: support pawn 0x... dismissed (the host's world, first co-op load)` for the
host world's pawns in its first 20 s in game; a machine whose party seats are full dismisses one per peer main pawn it seats
(`... a seat for a peer's main pawn`). Players' own main pawns are never dismissed. Not live-tested: `getPartyPawn` and the
PawnID literals SubPawn01/02 are read at first use (`PawnID literals missing` leaves the pawns alone).

## Campaigns and characters (launcher options popup)
Campaign: Own save (default), a campaign, New campaign (an empty world: the game starts a new one) or New from own save. Hosting a campaign logs `save_paths: after dir=...\coop\campaigns\<id>\win64_save` and the guests get that world; your own save is not touched. Character (when joining): Own save or a campaign whose Arisen was captured (host or play it once). DD2gether's campaigns and characters are imported at launcher start and from Settings, Import DD2gether (`Imported N DD2gether save(s)`); an imported character can be brought once its campaign was played.

A guest's progress goes back to its character file: after a load that was spliced and checked ok (`splice: ok`, no piece
refused), each of its saves is written to the character file (`profile: captured level=.. job=..`; the old file is kept as
`profile.dd2p.bak`) and the next load splices that. After a `MISMATCH` or a refused piece nothing is written, so a failed
splice never replaces the guest's character with the host's. With the save redirect not verified (`save_paths: REFUSED`)
nothing is spliced (`splice: contexts not spliced, the save redirect is not verified`). The host's saves reach the guests: after
a save the log shows `save_notice: save written, shared with the guests` and its launcher re-sends the files.

## Forced save load (guest; `load_slot`, `auto_load`)
The host picks the save slot on the hosting screen (Save: the slots of its mounted folder, labelled `Autosave / 12 Nov 19:31`, `Slot 1 / ...`; default the newest); it travels in the lobby and the launcher writes `load_slot=<n>` into every player's `coop\adapter.ini`. A guest's game loads that slot whichever row it chooses (Continue or Load Game): the log shows `save_load: forced slot N (asked M)`. A host, a solo game and a guest without `load_slot` load what they ask for; the system data load is never changed. A guest also cannot start a new world: its New Game rows do nothing (`title_menu: New Game refused`).
A co-op guest with a forced slot (`auto_load=0` turns it off, `auto_load=1` turns it on for anyone): once the title is up and idle for 2 s (MainFlowManager in TitleMenu, the title waiting for a key or a choice, the SaveService idle, the save redirect safe; no key press is needed) the plugin calls the game's own load, `MainFlowManager.requestStartType(StartType.Latest)`, the call the menu's Continue ends in: no row is selected and no menu handler runs, and the call reaches `requestLoad`, where the host's slot is put in (`title_load: Continue requested (try N, ...)`, then `title_load: the load went through`). A try that fails (an error phase, still at the title after 5 s, the call refused) is repeated up to 3 times; after that `title_load: the session's save did not load`, the GAME_STATE key `load=failed` and the overlay line "The session's save did not load". Notes: `docs/codemap/dd2.md` "Title load".

## Launcher report and overlay (co-op)
`game_state: place=..;char=..;save=..;slot=..;peers=..` is sent to the own launcher when the link comes up and on every change
(`place` title, loading or world; `char` off, ok, none, mismatch or refused; `save` off, ok or refused; `slot` the slot of
the newest game load, -1 before one, `load_slot: the game loads slot N` in the log; `peers` `<steamid>:<0 live|1 stale>`
joined by commas). A peer silent for 6 s is stale (`session: slot N silent for 6 s, treated as gone`, later `is sending
again`). In co-op the top left corner of the game shows each partner (title, loading, in world with the distance, connection
lost) and, when something is wrong, one line: `Your character could not be loaded: <why>`, `Save not protected: quit to the
title` or `Load slot N like the host` (a guest whose load slot differs from the one in the host's PLACE). The log shows
`overlay: panel registered`; a fault in the draw is logged and dumped and the panel stays off.

The capture launch writes `coop\capture.result`: `ok` or `failed: <code>: <detail>` with the code title, load, empty, redirect,
write or timeout; the 60 s run from the first time the title menu shows.
