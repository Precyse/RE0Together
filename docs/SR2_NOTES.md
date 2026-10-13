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
- Replication baselines (deltacomp) are compile-time 1 peer in co-op (hard part, later milestone); partner-singular code and Lua scripts (`REMOTE_PLAYER`) come after that.

## Juiced (DFEngine.dll)
Hooks session create, GameSpy code (0x81A590, 0x8360F9, 0xC1BC76, 0xD2526F), mempools, `lua_load_dynamic_script_buffer`, D3D9. It uses safetyhook and does not touch the co-op player limit. None of the addresses the adapter hooks appear in DFEngine. The start-up check compares each hooked function's first bytes, so a function already hooked by another patch is left alone.

## Adapter (milestone 1)
- Loads as `dinput8.dll` next to `SR2_pc.exe` (the exe imports DirectInput8; no such file ships).
- Files: `<game>\coop\sr2_adapter.log` and `<game>\coop\sr2_adapter.ini` (`max_players` 2..12, default 4; `trace` 0/1, default 1).
- Start-up check: PE time stamp, the first bytes of the five hooked functions, and the two `mov [limit], ebp` stores.
- Hook: `net_set_mode`; after the original returns for mode 1, both limits are set to `max_players`.
- Trace (trace=1, read-only): `session_pool_setup` entry (with the limits it sees and whether `net_set_mode` is still running), `join_queue_add` (queue count before/after), `player_slot_allocator` and `remote_player_getter` return value changes.

## Open questions for the live run
- Order of `net_set_mode` and `session_pool_setup` (log line `inside net_set_mode 1` means the post-return write is too late for the pool sizing).
- Where "session full" is decided; what a third `join_req` gets.
- Replication for peers 2 and 3 (deltacomp), partner semantics, Lua: later milestones.
