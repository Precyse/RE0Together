# Saints Row 2: notes for the 4-player co-op adapter

Full research plan (static, not in the repo): `C:\Users\ahmal\coop-scratch\sr2\NATIVE_COOP_4P.md`. Addresses are for the installed `SR2_pc.exe` (Steam build 1076239 with the Juiced patch; PE time stamp 0x49904C37; image base 0x400000, x86, large address aware). The adapter (`adapters/sr2`) refuses to patch an exe that differs (see "Start-up check").

## Confirmed findings
- All co-op netcode is in `SR2_pc.exe`: GameSpy SDK (redirected to OpenSpy by Juiced's `DFEngine.dll`), game port 4200. No Steam networking.
- The session layer (`pc_multi_session`) is built for 12 players; co-op is the same machinery configured down to 2.
- `net_set_mode` 0x7F6F50 (cdecl, one `int mode` argument, mode 1 = co-op) writes the per-mode limits. Its co-op case stores 2 into `[0xE8C434]` (max session players, store at 0x7F71EC) and `[0xE8C438]` (max players, store at 0x7F71F2); `[0xE8C448]` = 1 connection per player.
- `0x7F91C0` sizes the connection pool (`[0xE8C434]` x `[0xE8C448]`) and packet buffers from those limits; whether it runs before or after `net_set_mode` returns is not known yet (milestone 1 trace logs it).
- Player human pool: 12 slots at 0x2CD0590, allocator 0x9D7C10 refuses an index >= `[0xE8C438]`. `#PLAYER1#..#PLAYER12#` script names and `styletest_pc_MP2..MP12` assets already exist.
- `get_remote_player` 0x9D7BD0 (cdecl, no arguments): first human in the list `[0x217043C]` that is not the local player `[0x21703D4]`; about 700 call sites treat it as "the partner".
- Join: `join_req` handler 0x8162E0, queue add 0x810220 (stdcall(session, request); queue count at session +0x7EC, 12 entries).
- Replication baselines (deltacomp) are 1 peer in co-op; milestone 3 switches them to the 8-peer instances (see "Replication"). Partner-singular code and Lua scripts (`REMOTE_PLAYER`) come after that.

## Juiced (DFEngine.dll)
Hooks session create, GameSpy code (0x81A590, 0x8360F9, 0xC1BC76, 0xD2526F), mempools, `lua_load_dynamic_script_buffer`, D3D9. It uses safetyhook and does not touch the co-op player limit. None of the addresses the adapter hooks appear in DFEngine. The start-up check compares each hooked function's first bytes, so a function already hooked by another patch is left alone.

## Adapter (milestone 1)
- Loads as `dinput8.dll` next to `SR2_pc.exe` (the exe imports DirectInput8; no such file ships).
- Files: `<game>\coop\sr2_adapter.log` and `<game>\coop\sr2_adapter.ini` (`max_players` 2..8, default 4; `trace` 0/1, default 1).
- Start-up check: PE time stamp, the first bytes of the five hooked functions, and the two `mov [limit], ebp` stores.
- Hook: `net_set_mode`. Both limits are set to `max_players` by a thunk at the allocator-set activation call inside it (see "Replication"). `max_players` 2 patches nothing but the optional trace.
- Trace (trace=1, read-only): `session_pool_setup` entry (with the limits it sees and whether `net_set_mode` is still running), `join_queue_add` (queue count before/after), `player_slot_allocator` and `remote_player_getter` return value changes.

## Co-op player pools (milestone 1b)
- Live: Juiced's mempool watcher reported "Failed aligned alloc of 16384 bytes in coop_player cpu pool (max pool size 2260992)" with a second guest's data.
- Arena setup 0x51D800 creates every startup pool with `vtable[0x44]` (init 0xBF4A10: base, size, name, ...) on a base inside one arena. Pools that depend on the guest count (shipped for 1 guest): `coop_player cpu` object 0x2773F0C, size 0x228000 (`push` at 0x51E502, base arena+0x24D2450); `coop_player gpu` object 0x2773F68, size 0x1071000 (`push` at 0x51E8A7, base arena+0x10A30000). The cpu pool object links the gpu one (`[0x2773F60]`).
- `coop_allocators` (0x2773FC0, 0x5ADBB0) holds the deltacomp baselines; milestone 3 resizes it too (see "Replication"). The pools are packed in the arena (cpu pool ends where `coop_allocators` begins), so none can grow in place.
- Pool object (bump allocator, alloc 0xC00DB0): name +0xC, base +0x38, size +0x3C, used +0x44, mark +0x48, limit +0x4C (free = limit - used), capacity +0x50. Init also zeroes used and mark.
- Change: just before `net_set_mode(1)` runs its co-op case (which resets both pools), each pool gets a VirtualAlloc block of its current size x (max_players - 1) and the fields above are rewritten like init does. Scaling by guests because the shipped size is for one guest (host + 1). Done once per process; max_players 2 changes nothing. The current size is read, so a size Juiced set stays the base. Identity check at that moment: name and three equal size fields; otherwise the pool is left and the log says why.
- Start-up check also compares the two `push size` immediates.
- Juiced overlap: none of 0x2773F0C..0x2773F5C, 0x2773F68..0x2773FB8, 0x51E500..0x51E506, 0x51E8A5..0x51E8AB, 0xBF4A10..0xBF4A5D appears among the 3100 addresses in `df_sr2_targets.tsv`. Juiced hooks the alloc failure call 0xC00E4A and reads pool objects only to print the error.
- Live log: `pools: coop_player cpu 2260992 -> 6782976 bytes`, `pools: coop_player gpu 17240064 -> 51720192 bytes` (max_players 4).

## Replication (milestone 3)
Design and research tables (not in the repo): `C:\Users\ahmal\coop-scratch\sr2\M3_REPLICATION.md`, `m3_n1_to_n8.tsv`, `m3_peer_index_sites.tsv`, `m3_dc_sizes.tsv`. Corrects the plan: the deltacomp array index is the remote member's session slot (member byte +0x1B0, 0..max_players-1), not a peer count.
- Mechanism: co-op picks the 1-peer (N=1) `deltacomp_state_data_impl` at every alloc/free/free_all site (co-op flag), and forces index 0 at 32 more sites. The game also ships an N=8 instance of each of the 8 bundles (player, npc, vehicle, item, mover, prop, rocket, pipebomb), used by Strong Arm with slot indexing. Co-op is switched to those; `max_players` is at most 8.
- Patches (`replication_switch`, tables in `replication_sites.h`, applied together on the first co-op entry, only when `max_players` > 2):
  - 24 entry jumps: each N=1 alloc/free/free_all starts with `jmp` to its byte-identical N=8 twin (the 8 first bytes of both are checked).
  - 32 branch flips: the `je` (0x74) that forces peer index 0 in co-op becomes `jmp` (0xEB), so co-op reads the member slot.
  - 2 set names: the immediate of `mov edi, "coop-mp"` at 0x7F71D5 (activate) and 0x7F7083 (deactivate) becomes `"deltacomp_sa"` (0xE262B8), the set of the eight N=8 allocators.
  - 8 allocator fields: `+0x2C` (pool pointer source) of each N=8 allocator = 0xFA23A4, so co-op takes `coop_allocators` (which co-op stores in [0xFA23A4]) and Strong Arm keeps "mp sa deltacomp". Identity check first: `+8` = "deltacomp_sa", `+0x2C` = 0, `+0x14` = 0 (idle). The exe's static constructors write these objects, so this runs at the first co-op entry, not at start-up.
- Limit timing fix: the limits used to be raised after `net_set_mode` returned, but the player simulation objects and the player deltacomp pools are sized inside it from [0xE8C438]. The call at 0x7F7220 (allocator-set activation, `E8 BB FA FF FF` to 0x7F6CE0) now calls `limit_thunk`'s naked thunk (pushad, pushfd, write both limits, popfd, popad, jmp 0x7F6CE0), and the post-return write is gone.
- Pool: `coop_allocators` is rebased like the player pools (all three or none, name/size/empty check). New size = S0 + 7,131,240 - 16,320 + 35,160 x M + 7,492 x (M - 2) + 1 MiB (`coopAllocatorsPoolSize`): the N=8 fixed bundles replace the N=1 ones (9,207,000 - 2,075,760), the 2 shipped player elements go (15 x 2 x 0x220), M player elements x 15 x 0x928 come, plus 7,492 bytes per added player. 4 players: 13,225,024 + 1 MiB = 14,273,600. Never "mp sa deltacomp" (it overlaps the city master pool).
- Order inside the `net_set_mode(1)` detour: `coop_pools::resize` (three pools), `replication_switch::enable` (only when the pools were resized; otherwise co-op keeps the 1-peer code), the original. Activation then runs with the limits already raised.
- Start-up check: `code_check` covers the pool immediate (0x51E535), the npc limit store (0x7F71F8), the activation call and the two set-name loads; `replication_switch::sitesMatch` the 24 + 24 function starts, 32 branches and 2 immediates. Any difference: the adapter patches nothing. All of these bytes were also compared with the installed `SR2_pc.exe` on disk (read only).
- Slot guard (trace=1): `member_slots` walks the session member list (session [0x2528C14], list +0x400, next +0x22CC, local +0x408), logs slot (+0x1B0), state (+0x3C) and local flag when they change, and if a remote member's slot is outside 0..max_players-1 puts the 32 branches back to `je` (peer index 0; the N=8 code then serves one peer). It runs on join request queued, slot allocator and remote-player getter changes, and session pool setup. Without trace=1 there is no guard.
- Juiced overlap: none of the written addresses (24 entries, 32 branches, 0x7F71D6, 0x7F7084, 0x7F7220, 8 allocator fields, 0x2773FC0..0x2774014) is within 16 bytes of any value in `df_sr2_targets.tsv`. Read-only data (0xFA23A4, session pointer 0x2528C14) is referenced by Juiced and only read here.
- Not covered (milestone 4): partner-singular code (`get_remote_player` has 707 callers; the join tick 0x8CB490 and the chunk-loaded check 0x8CA3C0 look at the first partner only), so the third player may have no HUD and not hold the host while joining.
- Expected log, host alone (`max_players=4`, trace=1): `code check: exe matches the supported build`, `replication: 24 entries, 32 branches and 2 set names match`, `coop limits: activation call at 0x7F7220 retargeted`. Entering co-op: the two `pools: coop_player ...` lines, `pools: coop_allocators 5954480 -> 14273600 bytes`, `replication: 24 entries, 32 branches, 2 set names patched; 8 N=8 allocators follow the co-op pool`, `coop limits: max session players 2 -> 4, max players 2 -> 4` before `trace: session pools sized ... inside net_set_mode 1`, then `trace: coop_allocators used <less than 14273600> of 14273600 bytes, player N=8 allocator count 60 block <non-zero>`. With a guest: `trace: session members 2`, then one `trace: member 0x... slot 0 ... ` and one `slot 1 ...` line (local and remote); no `is outside the session slots` line. With `max_players=2`: `replication: off, max_players 2 is the shipped co-op, limits and pools are not patched`.

## Open questions for the live run
- Where "session full" is decided; what a third `join_req` gets.
- Member session slots in a real join (the milestone 3 gate), the coop_allocators high-water, partner semantics and Lua (milestone 4).

## Live results 2026-10-13 (solo host, SR2 Juiced, build 1076239)
- Milestone 1: entering co-op raised both limits 2 -> 4; the session pools sized with 4 (the write lands before the sizing). Without pool scaling, Juiced's watcher reported "Failed aligned alloc of 16384 bytes in coop_player cpu pool".
- Milestone 1b: hosting co-op resized coop_player cpu 2260992 -> 6782976 and gpu 17240064 -> 51720192, raised the limits, allocated the host's player slot, and reached "waiting for player" with no Juiced mempool warning.
- Not tested: any guest joining (needs other PCs); replication still tracks one remote peer.
