# SR2 milestone 9: Lua mission overrides for 3 and 4 players

The story mission scripts name exactly one partner (`REMOTE_PLAYER` = `"#PLAYER2#"`). The adapter loads a script from `<game>\coop\scripts` when a file of that name is there (AF9). This change builds those files.

## How it is built

- `python tools/sr2/lua_overrides.py <extracted scripts> <Juiced TU3 Mission Fixes folder> <out>`; `adapters/sr2/rebuild.cmd` runs it into `build\coop\scripts` when `SR2_LUA_SOURCE` and `SR2_LUA_FIXES` are set. Copy the folder into the host's `<game>\coop` (only the host runs missions). All of it is needed together: the mission files call the library added to `ug_lib.lua`.
- The game's scripts are never in the repo. The repo holds the generator, the Lua library (`tools/sr2/lua`) and the patch data (`tools/sr2/lua_patches`), applied to copies extracted read-only (`vpp.py extract patch.vpp_pc <dir> lua`). `em01.lua` and `rn07.lua` start from Juiced's copies.
- SR2 runs Lua 5.0 with the base library only (no table, string or math), so the library and the patches use plain Lua: `coop_count(list)` counts entries, appends are `list[coop_count(list) + 1] = value`. `lua_check.py` rejects `table.`, `string.`, `math.`, `os.`, `io.` and `debug.` in added code, and `lua_library_test.py` runs with those libraries removed.
- Every patch must find its text (line number plus expected text, or an expected count) or nothing is written.
- 48 scripts are generated: `ug_lib.lua`, 6 strongholds (M7) and 41 story scripts. All parse (Lua 5.1 parser) and the added code is Lua 5.0 style (`tools/sr2/lua_check.py`).

## ML1: the library appended to ug_lib.lua

| file | content |
|---|---|
| `coop_helpers.lua` | `coop_remote_players()` (the other names in `player_names_get_all()`; `REMOTE_PLAYER` when alone, so one or two players behave exactly like the original), `coop_is_remote_player`, any/all/near helpers (M7) |
| `coop_missions.lua` | `coop_player_table(local_value, remote_value)` (a name-keyed table with an entry, and a default on first read, for every remote), `coop_all_players_in`, `coop_any_remote(fn, args)` and the like (the call as written, `REMOTE_PLAYER` replaced by each remote), `coop_each_remote`, `coop_nearest_remote`, `coop_farthest_remote_dist`, `coop_expand_remote`, `coop_stop_remote_vehicles` |
| `coop_outfit_sync.lua` | `coop_sync_for(sync_table, player)`: a per-player sync mask (`PLAYER_SYNC[x]`) with the number of that player above its two bits (`mask = base + 16 * n`) while there is more than one remote player; the adapter's outfit sync sites read it (docs/SR2_NOTES.md, "Outfits, items and weapon slots (M16 W1)") |
| `coop_wrappers.lua` | `coop_wrap(name, rule)`: the global is replaced by a wrapper; with `REMOTE_PLAYER` among the arguments and more than one remote it applies the rule, else it makes the original call |
| `coop_rules.lua` | which function follows which rule |

Rules: `fanout` (once per remote; also `thread_new` and the ten `on_*` registrations), `seats` (the k-th remote takes seat + k; seat -1 stays), `group` (REMOTE_PLAYER in a character list becomes every remote), `target` (the remote nearest to the first argument), `all`, `any`, `nearest`. `keep` functions (`party_add`, `cutscene_play`, `get_char_vehicle_*`, `mission_debug`) are not wrapped. The `get_dist_*` helpers of ug_lib are wrapped around the raw `get_dist` so one call is not widened twice. The wrappers keep the originals in `COOP_NATIVE`; loops that must act on one remote call those (`coop_native(fn)`).

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
| FILE, patched | 128 | per-script edits |
| FILE, kept | 53 | see below |
| FILE, wrapper | 4 | `thread_new` fan-out |

Kept on purpose: navpoint-name constants (`..._REMOTE_PLAYER`), one nav or HUD value for the whole remote side (`teleport_coop(T[LOCAL], T[REMOTE])`, `HUD_BAR`, `PLAYER_TIMER`, cutscene end positions), `target = REMOTE_PLAYER` choices that stay on the default partner (rn07 lieutenants, bh11, ss01 waypoints, tss04 tow vehicle), and the comment block in ss01.

## The 160 `UG?` sites: defaults chosen

| rule | sites | default | patched instead |
|---|---|---|---|
| nearest | 59 | "a remote is within D" and "a remote is close" tests use the nearest remote | "a remote left" (bh09 ship, ss04, ss07 waits are left as nearest on purpose: a spread-out arrival must not hang a wait) use `coop_any_remote_farther_than` / `coop_farthest_remote_dist` |
| all | 64 | "wait until every remote is in the vehicle / equipped / ready" | "either player ..." tests (in vehicle, dead, shield, driver seat, fov, firearm) use `coop_any_remote`; "nobody sees it" tests use `not coop_any_remote` |
| seats | 26 | seat + k for the k-th remote, all sites | none; see the review list |
| target | 11 | the remote nearest to the NPC | none |

## Open review items

- Seats: P3/P4 take the seats after P2's. Where the script seats an NPC right after (bh01 speedboat seat 3, bh06 truck seat 1, rn04 van, rn07 helicopter, ss07) or the vehicle is small (rn03 and rn09 motorcycles, rn05 two-seat cart, which cannot carry P3), they may collide or fail. No seat-free test exists, so nothing falls back.
- The first remote is `"#PLAYER2#"`, the name of `REMOTE_PLAYER`. A mission helper that is called with that name and queries a wrapped function inside (`get_dist`, `character_is_in_vehicle`) gets the all/nearest answer over every remote for P2. Loops written in the patches use `COOP_NATIVE`, but the game's own per-player helpers do not.
- Index-based state is not name-keyed: ss06 (`*_INDEX`), tss01 `REMOTE_PLAYER_ON_2ND_ROOFTOP`, tss04 (two towed vehicles, jump counters) and the HUD bar / timer slots give P3 and P4 the remote side's slot.
- Two-player waits that the patches left on `nearest`: ss04 and ss07 teleport waits (offsets from a future position spread would hang `farthest`), bh01 win distance.
- Probe LW (spec): the engine passes the triggering human's name to the `on_*` callbacks for `#PLAYER3#`; the library assumes it.
- Callbacks, thread fan-out and the library need the whole folder installed; a mission override without the `ug_lib.lua` override fails on the missing `coop_*` functions.
