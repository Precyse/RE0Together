# DS2 handoff (2026-10-07)

Game: Death Stranding 2 (Decima engine), G:\SteamLibrary\steamapps\common\DEATH STRANDING 2 - ON THE BEACH. Not Dark Souls 2.

## Branches and worktrees
- main is the shipped line. ds2-qol and the older ds2-clean are merged or superseded.
- ds2-live (worktree C:/Users/ahmal/coop-ds2-live, head 8cb9826). Not merged. Holds the remote-body work (singleton guard, parking, E1/E2 ride work, HUD prompt fix, gear pile fix). One uncommitted change in its worktree.
- ds2-integration (worktree C:/Users/ahmal/coop-ds2-int, head 4df1331). Not merged. Adds gear_restore (off by default). Four uncommitted changes in its worktree.
- worktree-agent-a7c6ad2ff89d95ab4 (6209e9e): DS2 review branch, 13 commits, not merged. Holds the per-system cost, risk and verdict document.
- Agent worktrees under .claude/worktrees/ are leftovers. Do not delete without checking.
- Nothing is pushed. Do not push or merge unless asked.

## Current state of the remote body (ds2-live, unmerged, partly verified)
- Verified live: spawn with the singleton guard (3 of 3 clean in one run; the later run with the final guard crashed once, 16 ms after going live, at 0x140F75E12). Parking works (20 of 20 park/unpark cycles in the integration build). HUD prompt flicker fixed: 0 of 100 screenshots lost the "Activate Terminal" prompt (remote_guards clears DSPlayerComponent handler 0x14080ADE0 for the remote). Gear pile fix verified over two save/load cycles.
- Not verified: parked body collision, targeting and damage; the spawn crash rate under the final guard; a removal with the peer killed.
- Known root cause: the remote's DSPlayerEquipmentManageComponent and DSPlayerFacialRigManagerComponent write DSPlayerSystem's singletons at init (0x140F69970, 0x140E86A80) and clear them at remove (0x140F7A930, 0x140EE3EE0). Guard in player_system_guard.cpp redirects those writes for the remote. Removal of a body entity in a running world is unsafe (Entity::Remove clears DSPlayerSystem tables; the next update dereferences a cleared pointer), so bodies are parked (hidden, never removed) when the peer sends no pose for 5 s.
- Ride: use-vehicle step 0x141008970 (this, float, float, bool). Dropping the float args broke boarding; fixed in 24c2e22, not re-verified live. E1 (replace the GetAsyncKeyState F poll by hooking the game's Use Vehicle action 0x140E5B3F0, kind 3) not done. E2 (seat table keyed by vehicle resource UUID) not done.

## Open decisions and tasks (from the review; the user said proceed with all)
1. STRUCT_REQUEST 0x012A duplicated CUTSCENE_START. Renumbered to 0x0132 and committed on main, ds2-integration, ds2-tester-stale, ds2-world-sync and ds2-world-sync-rebased. Wire change: both peers must run builds with it.
2. Partner body follows from the simulation thread, not the overlay draw callback (marker_overlay.cpp:169).
3. Dead body after 3 s: still Entity::Remove plus rebuild (remote_player.cpp, diedLongAgo / forgetBody). Parking is only used for a peer that has gone. Parking a dead body does not help: no revive call is known, so it stays dead and hidden, and the next peer pose would show a corpse. Shipped as drop-and-rebuild. Risk: Entity::Remove in a running world can crash (the next update dereferences a cleared DSPlayerSystem pointer). Needs a live check with a peer killed.
4. Cargo rebuild: read the baggage pool once per tick; move rack_sync to the sim thread. Needs live checks for rack, terminal and vehicle bed.
5. Enemy keepalives only near the partner, or less often at distance.
- Smaller: vehicle placement onto the existing sim tick and drop the extra frame hook; gate the combat log behind diagnostics=1; the logger should not reopen the file per line; enable all hooks in one batch (89 hooks, 4.3 s start-up); decide whether PendingQueue and DelayLine are used.
- Riding as driver and passenger, draw/holster after the DSPlayerComponent cut, and rejoin reusing a parked body are not re-checked live.

## Live results 2026-10-13 (ds2-live fb0978e, solo with fake_launcher; logs and shots in coop-scratch/ds2_solo, ds2_peer, ds2_vehicle)
- Done on ds2-live: items 2, 4, 5 and all "Smaller" items (sim tick for body, vehicle and rack; keepalive by distance; combat log behind diagnostics; logger opened once; hooks enabled in one batch; PendingQueue, DelayLine and main_thread removed).
- PASS: start-up batch (85 hooks enabled 1.95 s after adapter start, no failures); partner body spawn and follow on the sim tick; park/unpark 5 of 5 cycles (parks about 8 s after link loss: 3 s until the peer counts as gone, then the 5 s timer); far-enemy keepalive every 10.07 s; combat log off with diagnostics=0; partner drives, boards, sits and gets off the truck; vehicle placement follows reported position and heading.
- NOT TESTED: near-partner keepalive (2 s, no enemy within 50 m); rack_sync and cargo transfers (no faults, but rack_sync logs only on change); dead-body path.
- Note: a vehicle placed at a reported height stays there after reports stop (the fake peer sent a stale height and the truck floated). A real peer reports the resting height, so this only shows with a fake peer.
- A direct DS2.exe launch exits at the game's Steam check ("Steam ProcessResult failed"); launch through Steam (gamectl.py launch). Game version is now v1.10.89.0.
- cleanup.sh deletes version.dll and the coop folder; never run it while the game is running.

## Dump investigation (answer owed to the user)
- HashDB.bin (5,629,349 bytes) has no readable asset names.
- LocalCacheWinGame\package\*\package.XX.XX.core (29 packages, 20.38 GB, header "DSAR" 03 00 01 00) looks like a compressed container with an unknown layout.
- Autosaves and coop\adapter.log are readable and were not analysed further. mdmp crash dumps are in coop\session\Documents.
- Conclusion: not yet concluded whether the dump is helpful. Recommend deciding after reading one autosave's structure, or dropping the question.

## Live test rules
- Only one game instance. Use fake_launcher.py as the peer only. Stop with cleanup.sh. No push.
- Before every launch: free commit above 25 GB (Get-CimInstance Win32_OperatingSystem). Reboot if RAM is short (steamwebhelper once held 5.6 GB).
- Lock: G:\coop-scratch\ds2\scripts\lock.sh for game runs.
- Never restart a live session or the PC to free leaked commit; restart Steam only.
- Never create or delete body cargo while the local player's slots are empty or gameplay has not settled.
- Session saves in coop\session can be refilled by test commands (loose.txt).
- Test scripts: G:/coop-scratch/ds2/scripts/ (reg.sh, park_cycle.sh, boot.sh, title_then.sh). Peer: tools/ds2/fake_launcher.py with --follow, --echo-anim, --echo-equip, --echo-weapon, --drive and --drive-pos.
- Analyst notes: tools/ds2/out/analysis/ (ENEMIES.md, WEATHER_TIME.md, ORDERS_CARGO_STRUCTURES.md). Older handoff: tools/ds2/out/HANDOFF.md.

## Constraints from the user
- Game files are read-only. Plain reads only.
- Do not commit or push unless asked.
- No bare `git stash`.
- Stop agents with TaskStop, not messages.
