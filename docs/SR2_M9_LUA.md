# SR2 milestone 9: Lua mission overrides for 3 and 4 players

The story mission scripts name exactly one partner (`REMOTE_PLAYER` = `"#PLAYER2#"`). The adapter loads a script from `<game>\coop\scripts` when a file of that name is there (AF9). This change builds those files.

## How it is built

- `python tools/sr2/lua_overrides.py <extracted scripts> <Juiced TU3 Mission Fixes folder> <out>`; `adapters/sr2/rebuild.cmd` runs it into `build\coop\scripts` when `SR2_LUA_SOURCE` and `SR2_LUA_FIXES` are set. Copy the folder into the host's `<game>\coop` (only the host runs missions). All of it is needed together: the mission files call the library added to `ug_lib.lua`.
- The game's scripts are never in the repo. The repo holds the generator, the Lua library (`tools/sr2/lua`) and the patch data (`tools/sr2/lua_patches`), applied to copies extracted read-only (`vpp.py extract patch.vpp_pc <dir> lua`). `em01.lua` and `rn07.lua` start from Juiced's copies.
- SR2 runs Lua 5.0 with the base library only (no table, string or math), so the library and the patches use plain Lua: `coop_count(list)` counts entries, appends are `list[coop_count(list) + 1] = value`. `lua_check.py` rejects `table.`, `string.`, `math.`, `os.`, `io.` and `debug.` in added code, and `lua_library_test.py` runs with those libraries removed.
- Every patch must find its text (line number plus expected text, or an expected count) or nothing is written.
- 50 scripts are generated: `ug_lib.lua`, 6 strongholds (M7) and 43 story scripts. All parse (Lua 5.1 parser) and the added code is Lua 5.0 style (`tools/sr2/lua_check.py`).

## ML1: the library appended to ug_lib.lua

| file | content |
|---|---|
| `coop_helpers.lua` | `coop_remote_players()` (the other names in `player_names_get_all()`; `REMOTE_PLAYER` when alone, so one or two players behave exactly like the original), `coop_is_remote_player`, any/all/near helpers (M7) |
| `coop_missions.lua` | `coop_player_table(local_value, remote_value)` (a name-keyed table with an entry, and a default on first read, for every remote), `coop_all_players_in`, `coop_any_remote(fn, args)` and the like (the call as written, `REMOTE_PLAYER` replaced by each remote), `coop_each_remote`, `coop_nearest_remote`, `coop_farthest_remote_dist`, `coop_expand_remote`, `coop_stop_remote_vehicles` |
| `coop_outfit_sync.lua` | `coop_sync_for(sync_table, player)`: a per-player sync mask (`PLAYER_SYNC[x]`) with the number of that player above its two bits (`mask = base + 16 * n`) while there is more than one remote player; the adapter's outfit sync sites read it (docs/SR2_NOTES.md, "Outfits, items and weapon slots (M16 W1)") |
| `coop_slots.lua` | per-remote slots (M16 wave 2): `coop_hud_slots`, `coop_each_remote_slot`, `coop_other_players` and the other-player queries, `coop_next_player`, `coop_trace` |
| `coop_wrappers.lua` | `coop_wrap(name, rule)`: the global is replaced by a wrapper; with `REMOTE_PLAYER` among the arguments and more than one remote it applies the rule, else it makes the original call |
| `coop_seats.lua` | the seat allocator: `COOP_RULE.seats` and `seat_lock`, `coop_seat_claim`, `coop_seat_in_vehicle`, `coop_seat_of` / `coop_all_seated` for the mission waits (milestone 16 W2, docs/SR2_NOTES.md "Vehicle seating") |
| `coop_waypoints.lua` | `coop_waypoint_add_for` / `coop_waypoint_remove_for` (a waypoint for one remote, named to the adapter with `mission_debug`), `coop_closest_navpoint` (milestone 16 W2, docs/SR2_NOTES.md "HUD and waypoints") |
| `coop_rules.lua` | which function follows which rule |

Rules: `fanout` (once per remote; also `thread_new` and the ten `on_*` registrations), `seats` (the seat allocator `coop_seats.lua`: the first remote takes the given seat, every other one a free seat or stays on foot; seat -1 and no seat stay), `seat_lock` (as `fanout`, except to a remote on foot for lack of a seat), `group` (REMOTE_PLAYER in a character list becomes every remote), `target` (the remote nearest to the first argument), `all`, `any`, `nearest`. `keep` functions (`party_add`, `cutscene_play`, `get_char_vehicle_*`, `mission_debug`) are not wrapped. The `get_dist_*` helpers of ug_lib are wrapped around the raw `get_dist` so one call is not widened twice. The wrappers keep the originals in `COOP_NATIVE`; loops that must act on one remote call those (`coop_native(fn)`).

Verified by `tools/sr2/lua_library_test.py` (lupa, mock natives): with 2 players every wrapped call reaches the original once with the same arguments and result; with 3 players each rule gives the expected calls; a wrapper is never wrapped twice.

## ML2 to ML53: the story scripts

Generic rules (`lua_patches/generic.py`) rewrite what always changes the same way: `x == REMOTE_PLAYER` and `~=` become `coop_is_remote_player(x)`, `{[LOCAL_PLAYER] = a, [REMOTE_PLAYER] = b}` becomes `coop_player_table(a, b)`, `T[LOCAL_PLAYER] and/or T[REMOTE_PLAYER]` become `coop_all_players_in(T)` / `coop_any_player_in(T)`. Per-script patches (`missions_*.py`) do the rest: reads of `T[REMOTE_PLAYER]` become all/any over the remotes, calls of mission-local helpers become `coop_each_remote(f, ...)`, per-player threads and cleanup loops run for every remote, and the 63 `UG?` sites whose default was wrong use `coop_any_remote(...)`, `coop_all_remotes(...)` or a farthest/any-far test.

## Outfit, item and weapon-slot masks (M16 W1)

The generic rule `customization_item_wear(..., PLAYER_SYNC[x])` and `inv_weapon_enable_or_disable_all_slots(..., PLAYER_SYNC[x])` becomes `coop_sync_for(PLAYER_SYNC, x)`. It matches 5 places: rn02 897 (the clothing dealer), ep02 460 and 482 and rn08 1302 and 1336 (the bomb plant). rn02 934 to 936 is a block edit: the gun is equipped for each remote that is not in a driver seat, with `COOP_NATIVE` calls so the wrappers do not widen them again.

## Coverage of the uses

`SR2_M9_LUA_USES.tsv` lists every use (file, line in the extracted script, class, callee, rule, cover, decision). 891 uses, not 883: Juiced's `em01.lua` and `rn07.lua` carry 8 more.

| cover | uses | how |
|---|---|---|
| UG, wrapper | 319 | answered by the library (fanout, callbacks, thread starts) |
| UG, keep | 12 | stay on the default partner (`party_add`, `cutscene_play`, ...) |
| UG? | 160 | default rule, 63 of them patched where the default was wrong |
| FILE, generic rule | 215 | name tests, tables, and/or pairs |
| FILE, patched | 149 | per-script edits (22 more since M16 wave 2) |
| FILE, kept | 32 | see below |
| FILE, wrapper | 4 | `thread_new` fan-out |

Kept on purpose: navpoint-name constants (`..._REMOTE_PLAYER`), one nav value for the whole remote side (`teleport_coop(T[LOCAL], T[REMOTE])`, cutscene end positions), `target = REMOTE_PLAYER` choices that stay on the default partner (`party_add`, the tss04 tow vehicle), and the comment block in ss01. The HUD slots and the other-player choices that M9 kept moved to M16 wave 2 below.

## The 160 `UG?` sites: defaults chosen

| rule | sites | default | patched instead |
|---|---|---|---|
| nearest | 59 | "a remote is within D" and "a remote is close" tests use the nearest remote | "a remote left" (bh09 ship, ss04, ss07 waits are left as nearest on purpose: a spread-out arrival must not hang a wait) use `coop_any_remote_farther_than` / `coop_farthest_remote_dist` |
| all | 64 | "wait until every remote is in the vehicle / equipped / ready" | "either player ..." tests (in vehicle, dead, shield, driver seat, fov, firearm) use `coop_any_remote`; "nobody sees it" tests use `not coop_any_remote` |
| seats | 26 | the first remote keeps the script's seat, the others take free seats (M16 wave 2) | none: the seat allocator `coop_seats.lua` asks the game who sits where, see below |
| target | 11 | the remote nearest to the NPC | none |

## M16 wave 2: per-remote slots, seats, other players, the parameter rule

Four rules replace the single partner that M9 kept. The two-player result is unchanged by each: with one remote every wrapper makes the shipped call and every table, slot and seat reads as before.

- **Parameter rule (`lua_params.py`).** In every script but `ug_lib.lua`, a call of a wrapped function (`coop_rules.lua`) that is given a parameter of an enclosing function and not the literal `REMOTE_PLAYER` becomes `coop_native(f)(...)`. A parameter holds one player (a callback gets the triggering human's name, the per-remote helpers pass one remote per call), so a trigger callback for P2 no longer teleports or freezes P3 and P4. `lua_overrides.py` prints `params: N calls in K scripts`. `risky_calls` lists the one helper that is still called with the literal (`rn05_wait_for_vehicle_entry(passenger)`: only P2 fits the two-seat cart).
- **HUD slots (`coop_hud_slots(local_slot, remote_slots)`).** The game has four slots of each HUD element kind (bars, timers, objective texts, x-of-y). `#PLAYERn#` takes `remote_slots[n - 1]`, a remote without an entry shares the first. ep01 bars, ep02 and rn11 failure timers, the tss01 searchlight timer, the tss04 stunt jump and ss06 kill objectives use it; `coop_each_remote_slot(fn, slots)` stops, clears or switches off once per distinct remote slot. ss06 P4 shares P2's objective slot (slots 0 and 1 are the mission's). The slots stay `SYNC_REMOTE` elements, so every remote machine shows every remote's element (cosmetic).
- **Seats.** The seat allocator of `coop_seats.lua` (`docs/SR2_NOTES.md`, "Vehicle seating"): the first remote takes the seat the script gives, every other one a free seat of the vehicle or of another vehicle, or stays on foot; the in-vehicle query follows the placement, so a full cart or boat does not hang a wait. rn01, rn02, em01 and rn09 take the seat of each remote with `coop_seat_of` / `coop_all_seated` (a remote on foot or in another vehicle has none and is not waited for). The wave 2 Lua branch had its own seats rule (`coop_reserve_seats` at 17 sites, `COOP_SEATS`, an `in_vehicle` rule); it was dropped at the merge because the seating branch covers the same calls (the reservation hints are not needed: the allocator reads the game).
- **Name-keyed state and "the other player".** `coop_other_players(name)` replaces `OTHER_PLAYER[x]`; ss06, tss01, tss04 and rn07 state is keyed by player name; ep01 sequence joining, bh01, bh05, bh07, bh10, bh11, rn03, ss01, ss02 and tss02 look at every other player; `sync_from_player` gives any remote `SYNC_REMOTE`; `get_dist_closest_player_to_object` measures with the game's `get_dist`.

**Per-remote waypoints (M18).** A GPS a script sets through a local sync variable now goes to its own remote: 25 line edits (`lua_patches/waypoints.py`, `per_player_waypoints`) in bh08, ep02, rn02, tss02, ss01, ss06 and ss10 turn `waypoint_add(nav, sync)` and the like into `coop_waypoint_add_for(player, waypoint_add, nav, sync)`, `player` being the player parameter of the enclosing function. The three tss04 sends of the one remote tow stay broadcast. For one remote `coop_waypoints.lua` makes ug_lib's `mission_waypoint_add` / `mission_waypoint_remove` from the natives (the remove always reaches the player and leaves the `Mission_waypoint` record, the add sets it), because a remove for P2 used to clear the record and skip P3's. With one remote every rewritten call is the original call.

`coop_trace(text)` calls the no-op native `mission_debug("coop: " .. text)`; the adapter writes it to the log under `trace=1` (`docs/SR2_NOTES.md`, "Lua per-remote slots and LT1 (M16 wave 2)").

## Open review items

- Seats: handled by the seat allocator (`coop_seats.lua`, milestone 16 W2); its live gates are open, see `docs/SR2_NOTES.md` "Vehicle seating".
- The first remote is `"#PLAYER2#"`; with three and four players the remotes are `#PLAYER3#` and `#PLAYER4#` (the pool hands out the lowest free number, verified from the exe) and the engine passes those names to the `on_*` callbacks (live gate LG1, probe LW).
- Two-player waits that the patches left on `nearest`: ss04 and ss07 teleport waits (offsets from a future position spread would hang `farthest`).
- Kept on purpose: one remote nav per teleport and cutscene end (`teleport_coop`), `party_add` with one leader, the one remote towed vehicle in tss04, rn07 lieutenant debug lines, the bh05 player voice lines, bh07 `mission_help_table(..., OTHER_PLAYER[...])` (a shipped bug that shows nothing), rn02 free clothing (reaches every remote, cosmetic), and markers, waypoints and help text sent with `SYNC_REMOTE` (shown on every remote).
- ss10's repair outfit already reaches every remote (`customization_outfit_wear` with `SYNC_REMOTE`), so it needs no change.
- Callbacks, thread fan-out and the library need the whole folder installed (every player needs the new `coop\scripts` folder: the version gate hashes it); a mission override without the `ug_lib.lua` override fails on the missing `coop_*` functions.
