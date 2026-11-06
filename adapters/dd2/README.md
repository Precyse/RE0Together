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
host side on, guest side on` (if the guest side is off, the line before it names setTime's parameters), then with a peer:
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
(`enemy_spawn: stopped at <step>` names a refusal), the mirror's HP bar follows the host's. Away from the host the guest's
own enemies are removed (no enemies are simulated away from the host yet).

## Campaigns and characters (launcher options popup)
Campaign: Own save (default), a campaign, New campaign (an empty world: the game starts a new one) or New from own save. Hosting a campaign logs `save_paths: after dir=...\coop\campaigns\<id>\win64_save` and the guests get that world; your own save is not touched. Character (when joining): Own save or a campaign whose Arisen was captured (host or play it once). DD2gether's campaigns and characters are imported at launcher start and from Settings, Import DD2gether (`Imported N DD2gether save(s)`); an imported character can be brought once its campaign was played.

Progress made during co-op is not carried back yet (journal and write-back come with the XP and loot phase); the next
solo save captures the real save again.
