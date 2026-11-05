# RE0 co-op handoff (2026-10-25 evening)

Repo worktree `C:\Users\ahmal\coop-next`, branch pushed to `next` (Precyse/RE0Together; each push builds a preview release). Last pushed: `d48a3d3` (lift), `4b09aca` (reload fix) after `1297749` (launcher); Build 208 = `a1bf7b5`. Working tree clean at handoff except this file.

## How to work here
- Build adapter: `cmd //c "<scratchpad>\b.bat"` (VsDevCmd x86, cmake build in `adapters/re0`, prints only errors); copy `adapters/re0/build/dinput8.dll` to `adapters/re0/dinput8.dll`; tests = every `adapters/re0/build/*_test.exe` except `net_test`, `overlay_test` (87 pass).
- Launcher tests: `DOTNET_ROOT=C:/Users/ahmal/.dotnet-sdk dotnet run --project tools/launcher_tests/LauncherTests.csproj`. Screenshots: `python tools/launcher_web/render.py <fixture> --out <dir>`.
- `python tools/publish_check.py` before every commit. Commit trailer: `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` + `Claude-Session: ...`.
- Release poll: GitHub API `actions/runs?branch=next` with the token in the old session's scratchpad `gh.env` (recreate if gone).
- Logs (read only): host `F:\SteamLibrary\steamapps\common\Resident Evil 0\coop\adapter.log`, guest = newest `peer_*.log` there (forwarded by the launcher).
- RE tools: decomp export `C:\Users\ahmal\coop-scratch\re0\decomp` (index.tsv, funcs/*.c, vtables.tsv, strings.tsv); capstone `coop-scratch\re0\menu_tools\rdis.py <addr> [size]` (image `C:\Users\ahmal\coop-framework\tools\re0\image.bin`); message dumps `menu_tools\arc.py` + `gmd.py`.
- Game-breaking fixes (crash, freeze, soft-lock, stuck player): commit + push `next` without asking. Everything else waits for "push". Traces: onset/clear lines, line budget, summary only when nonzero.

## Shipped this session (Builds 199 to 208 + launcher 1297749, none verified live unless noted)
- Shotgun pellets through partner zombies: real cause was observed enemies' hit shapes never rebuilt (their slot-41 update is skipped). `game::kHitShapeBuilders` + `enemy_observer_game::refreshHitShapes` after each observed move, all classes except uEnemy2b (attack points only) and uEnemy34 (builder needs AI state; open). Skip bits +0x6a0c also re-asserted after the move.
- `enemy_drift` trace (drift onsets, corrections, per-shot anomalies, 30 s summary) and `shot_probe` (missed wide scans: nearest shape vs ray, capsule length).
- Dying enemies posed every round for 5 s; observed enemies hold the simulator's still frame (`SeekTracker` holdsStillOwner, `Verdict::Hold`).
- Partner tag gated (both in play, state < 2 s); Co-op page opens after game over; load-in flavour lines (commonmsg 0x58..0x5f) skipped (`load_flavor`).
- Guest placed where the game put it after a second save (join_sync switch mode) and no stale pose resend.
- TEAM door follow: answers read before timeout, busy follower defers 5 s (`room_move_rule::defers`).
- Peer floor drops load the item's model set {0x71, id} first; `scene::move` restores the loaded room's message table (0x61dbe0) - fixed wrong door/ladder prompts.
- Partner tag shows room names (`room_names.h`, 107/159).
- Guest no longer thrown out of the world reloaded after a game over; reloads keep the save's inventory (`4b09aca`).
- Launcher: in-game ID cards, coherent mismatch card, LEAVING 0x0014 / KICK 0x0015, Remove button.

## Open
1. Lift (sh0e 0x32, trigger 10, demo 31): FIXED and pushed (cutscene starts on the follower when it reaches it; world position for a rider attached to the lift; timed loops run on until this machine's count ends). Not run live; check the probe line `event_sync: followed serial ... loop at pc ... runs on here` and docs/RE0_NOTES.md "The sh0e lift ride on both machines".
2. Reload path: FIXED and pushed in `4b09aca` (guest's host_exit wait fired at its own game over and exited the reloaded world; game-over inventory messages overwrote the save). Not run live. Leftovers noted in docs/RE0_NOTES.md "Game over then a reload": equip_refresh pending re-equip, host_exit request kept at the title, auto_join muting the keyboard at the guest's game over, partner_hud max HP across reloads.
3. uEnemy34 hit shapes (only if its shots miss: check `shot_probe:` lines).
4. Next live test: lift ride both ways, game over then Co-op page reload, second save load (inventory), partner shotgun hits, dropped weapons, door/ladder prompts, room names in the partner tag, launcher leave/kick/mismatch card.

Details for every item above: `docs/RE0_NOTES.md` (sections dated 2026-10-25) and `docs/codemap/re0.md`.
