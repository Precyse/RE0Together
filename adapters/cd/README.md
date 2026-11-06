# Crimson Desert adapter

`bin64\winmm.dll` in the game folder: a winmm proxy that runs inside `CrimsonDesert.exe` only. Phase 1 of the design
(`coop-scratch\cd\design`): it hooks the two client byte funnels and the main-thread tick, sniffs every Troc message by
name, runs the world gate, talks to the launcher and shows the other player as a copy (Acks injected into our own
client only; nothing real is dropped or rewritten, our server is never written).

Supported build: PE TimeDateStamp 0x6AEB7D59 (Steam build 26448710). On any other exe it installs nothing and the game
plays vanilla. File map: `docs/codemap/cd.md`.

## Build
VsDevCmd `-arch=amd64`, then `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build` and
`ctest --test-dir build`. The DLL lands in `adapters/cd/winmm.dll`.

## Install by hand (one PC, no launcher)
Copy `winmm.dll` to `<Crimson Desert>\bin64\` (check there is no other `winmm.dll` there first: an ASI loader or cdmp).
Remove it to go back to vanilla. Settings: `bin64\coop\adapter.ini` (`capture=1` writes a packet capture,
`dev_commands=1` reads `bin64\coop\cmd.txt`).

## Phase 1 check (probe L01)
1. `capture=1`, start the game, play 30 minutes: city, a camp fight, loot, ride, climb, glide, fast travel, a death,
   a save.
2. `bin64\coop\adapter.log` must show: `build_gate: ... supported`, `troc_catalogue: 1175 messages, 0 duplicates, 0
   required missing`, `funnels: 2 patched`, `tick: 1 patched`, `main_tick: runs on thread N` (once), world gate lines for
   every load (`Title -> Loading`, `-> AwaitLocalReady`, `-> Ready`), and census lines with no `malformed` or `faults`.
3. `python -I tools/cd/capture_dump.py <capture.cdcap> <bin64\coop\troc_catalogue.tsv> --summary` lists every message
   seen and its exact payload sizes: they fill `size_table.h` in the next phase.

## Launcher status (GAME_STATE)
The adapter tells its launcher where the game is and whether the partner's copy is shown (`adapter.log` line
`game_state: place=...;copy=ok|off|refused:<code>;peers=...`). Codes: `silent` (no frame from the peer for 8 s; the copy
is removed and comes back with the next frame), `template`, `recipe`, `names`. A peer whose adapter differs from ours
(`peers` state 2) reads "Mod differs" on its card. `adapter.log` also says `waiting for the title (WorldSystem)` while the
game boots, and `conflict: cdmp present: co-op off` when `bin64\cdmp.asi` is still beside the exe (the launcher archives
cdmp at install; by hand, move it away).

## Presence check (probe L04 and the phase 3 gate, two PCs)
`presence=1` (default). Both players load their own save; each `adapter.log` shows `presence_send: own template
generation 1`, then on the other machine `presence_recv: slot N copy spawned ...`. CD_COUNTERS (in the log of both) must
show templates, moves and actions sent and applied non-zero, and `inject dropped` 0. Actions naming a mount or a target
are held back (counter `actions held`) until the key map exists. If a Req size differs on this build the log says
`presence_send: ... the recipe expects ...: presence off` and nothing is injected. `presence=0` keeps a machine sniff-only.

## Story (two PCs, same quest)
Default: the host logs `story: ... apply acks (requested branch at +13 N, at +17 N ...)` every 30 s while stages
change, and the guest logs `story: would replay key 0x... branch N ..., branch table count N, entry ok` once per host
branch its own world has the stage for. Check that "+13" counts the host's own requests (the branch field) and that
the guest's table counts are small and above every branch logged. Only then set `story_replay=1` on the guest: the
lines read `replaying`, the 30 s line counts `posted` and `applied`, and `save_guard: on` holds autosaves while a
replay waits (`save_guard: hold reached 30 s` means a replay was abandoned).
