# RE0 HD Remaster: reverse-engineering notes

Build: Steam app 339340, buildid 17178773, `re0hd.exe` 32-bit, image base 0x400000, **no ASLR** (addresses below are absolute). SteamStub-wrapped: `.text` is encrypted on disk and decrypted in memory before the game's own code runs. Adapter hooks must wait for decryption. Found with `tools/re0/probe.py` and `tools/re0/disasm.py` (runtime reads only).

## MT Framework type info
- Every class vtable slot 4 is `getDTI()` = `mov eax, <dti>; ret`. The DTI's field +4 is the class-name string.

## Globals (pointer to singleton)
| address | singleton |
|---|---|
| 0xdcbf3c | sPlayer* |
| 0xdcc0d0 | sGameChara* |
| 0xe2d7c8 | sGamePad* |

## sPlayer
| offset | meaning |
|---|---|
| +0x2c | controlled character (uPlayerBase*) |
| +0x3c | partner character (uPlayerBase*) |

The V key (partner switch) swaps +0x2c/+0x3c. sGameChara mirrors them at +0x148c/+0x149c.

## uPlayerBase (uPlayerBilly / uPlayerRebecca), about 0x6AD0 bytes
| offset | meaning |
|---|---|
| +0x40 | position vec3 (+pad) |
| +0x50 | rotation quaternion xyzw |
| +0x60 | scale vec3 |
| +0x70 | world matrix 4x4 (current); +0xB0 previous |
| +0x498..+0x4f6 | **candidate** action/motion state (unconfirmed): +0x49a byte 0 idle / 3 walking; +0x4a4 byte 0x0a idle / 0x04 walking; +0x4e0 float 8.0 idle / 6.0 walking; +0x1880/+0x1884 change with walk. Confirm with the tracer |
| +0x67b8 | think (brain) object: cPlayerThink (vtable 0xccbbe8) when controlled, cPlayerSubThink (vtable 0xcc99b8) when partner. V swaps them. Setter at 0x508b1e |

## Think (brain) interface
- Slot 20 (+0x50) = which pad to read. cPlayerThink 0x4f9f10 → `sGamePad::getPad(0)`; cPlayerSubThink 0x4f4be0 → `getPad(1)` (the partner's arrow-key pad).
- `sGamePad::getPad(i)` at 0x600630 (thiscall, 1 stack arg, i < 2) returns the pointer at `sGamePad + 0xc24 + i*8`, or 0 while input is blocked.
- Player code calls think slots +0x1c, +0x20, +0x28, +0x38 on the character's think. +0x38 is a target-eligibility filter (loop of ≤ 20), not input.

## Think lifecycle (confirmed)
- `sPlayer::setControlled(player)` 0x4ecda0: `alloc(0xd0, 0x10)` (cdecl 0x4f81e0), `cPlayerThink` ctor 0x4f8010 (thiscall, returns this), `player->setThink(t)` 0x50f980 (thiscall, ret 4, destroys the old think), `sPlayer+0x2c = player`.
- `sPlayer::setPartner(player)` 0x4ece00: same flow with `cPlayerSubThink` (alloc 0x4f3760 size 0x1a0, ctor 0x4f3540), `sPlayer+0x3c = player`.
- V recreates both thinks; it does not swap objects.
- cPlayerThink DTI 0xdcebac. DTI vtable slot 1 = newInstance.

## Per-frame (tracer, 60 fps)
- `uPlayerBase::move` 0x513190 (thiscall, no args), vtable slot 41 (Billy reaches it through a wrapper at 0x51b683). It calls think slots 5, 6, 16, 21, 25 once per frame.
- cPlayerThink calls its getPad (slot 20) about 12x/frame, e.g. 0x4fa420: `pad = think->getPad(); return pad->vfunc[9]() == 1`.

## Pad object (vtable 0xce55e0, 36 slots)
- Slots 5..35 are action queries forwarding to two inner devices at +4 and +8 (keyboard/joypad), OR'ed. Most return bool.
- Slot 14 returns a struct via hidden out-pointer (`ret 4`), probably an analog vector.
- Slots 8/9/14/15 first check `sGamePad+0xbcd` and `0x5fe490(this+8)`.
- **Network pad plan:** an object with a copied vtable whose slots 5..35 return the remote player's recorded answers. The partner's cPlayerThink gets it via its getPad (slot 20).

## Co-op control model (decided 2026-09-29, revised)
- Fixed ownership: the host always owns Rebecca and the first peer always owns Billy, whoever is focused. When the player objects change (session start, save load) the host focuses Rebecca once through the swap helper; camera_parity mirrors it to the guest.
- Control rule (`character_owner::controlOf`, pure part in `control_rule.h`): no peer is vanilla; an unknown owner is vanilla on the host and Locked on a guest; both characters stay with their owners (Local or Remote) in both party modes; a Remote character is Locked while its owner reports another room than the one loaded here (its input belongs to that room). TEAM = shared camera and following; LEAVE_BEHIND = independent play (each machine focuses its own character, nobody follows). Damage, inventory and state sync follow ownership only (`isRemoteOwned`/`isLocalOwned`).
- PLAYER_STATE (44 bytes) describes the sender's owned character (position, hp, characterId), whatever the camera is on; `focusedCharacterId` carries the camera character.
- Menus stay with the focused character's owner (vanilla): the other player presses V to take focus before opening their own inventory.
- Party commands are adapter-level because the game reads V/E from the focused character's think, whose input on a non-owner's machine is the remote pad. `command_input` polls KC_change (V) and KC_trace (E) from `%LOCALAPPDATA%\CAPCOM\RESIDENT EVIL 0 HD REMASTER\config.ini` with GetAsyncKeyState edges while the game window is foreground. V sends SWITCH_REQUEST 0x0103 (the host applies it directly for itself); the game's own V can still fire, so the host undoes a game-side focus change within 500 ms of an adapter switch. E toggles the party mode: PARTY_REQUEST 0x0109 to the host, which flips and broadcasts PARTY_MODE 0x010A (reliable, also every 2 s); toasts "Team" / "Leave behind: <character> waits". The vanilla E toggles sPlayer +0x40 (u8, 1 = partner follows); party_mode now owns that byte while a peer is connected.
- Vanilla camera model: both clients keep the same controlled character (sPlayer+0x2c) and show the same room. The owner of each character drives it with their pad; the other client replays that input.
- If the partner is in another room it idles as in vanilla. Split-room simulation is deferred (too much data).
- The V switch changes the active character. Only the owner ever drives a character.
- One PC cannot run two RE0 instances: SteamStub ends the second one. Testing uses the echo peer, and real tests need two Steam accounts that each own RE0.

## Proven in-game (2026-09-29)
- Giving the partner a cPlayerThink (alloc + ctor + setThink) is stable, and the partner then obeys the pad exactly like the controlled character (identical 90° turn, both walk). **Co-op strategy: the remote player drives the partner via input replay; state sync only corrects drift.**

- **Network input replay works end to end** (echo test, 1 s delay): the partner reproduces the sender's turns and walks with native animation; drift snaps are rare.
- `getPad` returns a dummy pad (`sGamePad+0xc34`, vtable 0xce58b8) while input is blocked; only vtable 0xce55e0 objects are real pads.
- The joypad's stick query (slot 14) writes its out-vector with `movaps`, so the buffer must be 16-byte aligned.

## Enemies (found 2026-09-29, hardware write breakpoint via tools/re0/watch_write.py)
- Classes uEnemyNN, object about 0x6cc0 bytes, same transform layout as players (+0x40 pos, +0x50 quat).
- **HP: i32 at +0x1030** (fresh uEnemy11/12/16 = 94, handgun hit = 16). Dead enemies hold -1, re-written every frame by their update (0x526c7d → 0x42105d → 0x41eb60 → 0x424a2a).
- `setHP(this, i32)` 0x529310 (thiscall, ret 4). It is called by per-class code.
- **Damage entry: enemy vtable slot 35 (+0x8c)**, `damage(attacker, float distance, HitInfo*)` thiscall ret 0xC. uEnemy11 implements it at 0x4200c0.
- Hit resolver 0x5313b0 (ret 0x18) builds `HitInfo { i32 rangeTier; i32 attackType; i32 a; i32 b; void* attacker; u8 flag }` and calls slot 35. rangeTier 0..3 comes from distance thresholds. attackType 0x1b takes a special path through sPlayer (0x4eca10). Caller chain: 0x7d4d20 → 0x542710 → 0x52f520 → 0x5313b0.
- **sEnemy\*** global at 0xdcdc78. Its enemy pool is at sEnemy+0x4b0: 16-byte entries `{vtable 0xcbd01c, next, prev, uEnemy*}`, 37 slots (+0x4a8 = 0x25). **Pool slot index = enemy network id** (same save and room means the same spawn order).
- The 38 enemy class vtables (slot 35 = damage; 18 distinct implementations): 0xcbdcd8, 0xcc4f28, 0xcc50a0, 0xcc5218, 0xcc5390, 0xcc5508, 0xcc5680, 0xcc57f8, 0xcc5970, 0xcc3fe8, 0xcbf0d0, 0xcbf5f8, 0xcbf9a0, 0xcbfbc8, 0xcbfe58, 0xcc01a8, 0xcc0458, 0xcc08c0, 0xcc5ae8, 0xcc0cb0, 0xcc13d8, 0xcc1618, 0xcc18e8, 0xcc5c60, 0xcc1ce8, 0xcc1e60, 0xcc1fd8, 0xcc25d8, 0xcc2b38, 0xcc2f70, 0xcc3248, 0xcc3710, 0xcc5dd8, 0xcc5f50, 0xcc60c8, 0xcc6240, 0xcc63b8, 0xcc6530.
- Co-op plan: the guest sends {enemy id, attackType, a, b, rangeTier, distance, attacker character}. The host calls slot 35 with its own object for that character. Still open: a stable enemy id (index in sEnemy's list), and enemy state replication to the guest.
- Player damage: player HP is i32 at +0x1030 (Rebecca 150, Billy 161 seen), set with the same setHP 0x529310. `uPlayerBase::takeDamage(a, b)` 0x50fb30 (thiscall, 2 stack args, ret 8) is called from the player's own update, takes the amount from sEnemy 0x416de0 and floors HP at 1. Enemies also call it from their own update (0x76f9e8 → 0x5127c5).
- **All player hits (damage and death) go through `uPlayerBase::processHits` 0x512080** (vtable slot 38, thiscall, no args), called from the enemy attack code 0x76f910. Normal hits call takeDamage. The lethal hit calls think slot 6 → 0x4fcea0 (cPlayerThink slot 28) → 0x4fd010, which does setHP(0). The adapter hooks processHits and skips it for a remote-owned character, and PLAYER_STATE carries the owner's HP.
- Ownership gate (2026-09-29): damage also reaches players through queued damage in their own update (0x513570 → 0x4fd160 → takeDamage) and the lethal hit (think->changeState 0x4fb2a0 → cPlayerThink slot 28 `onDeath(player)` 0x4fcea0 → killPlayer 0x4fd010 → setHP 0). So the adapter gates **setHP** (remote-owned players ignore non-authoritative writes) and **onDeath** (skipped for remote-owned characters; the owner sends PLAYER_DIED 0x0120 and the other machine replays onDeath). The partner's cPlayerThink is allocated at 0x1a0 and zero-filled.
- Crash 2026-09-29: heap corruption (0xc0000374) when the echo-driven partner Billy "died"; root cause not confirmed.

## Rooms, doors, menus, saves (2026-09-29)
- **Door entry point:** every door calls `sRoomControl::changeRoom(room, entry, flags)` at 0x610c60 (thiscall on sRoomControl 0xdcbeb4, ret 0xC; the only caller is the door thunk 0x552720, flags 0x20001, room/entry from the door object +0x20/+0x24). It calls 0x61e2c0 on sSceneInfo (0xdcbf40), which carries the partner (sPlayer +0x3c) along when sPlayer +0x40 (follow) is set and partner +0xff4 equals the scene's current room record (sSceneInfo-side +0x20; record +8 = room id). The carry repositions the partner (writes at 0x61edcb / 0x61b321) and does not change partner +0xc or +0xff4 (+0xff4 is the same object in both rooms). Writing +0xff4/position by hand leaves the game half-updated (E refused, crash on the partner's next door).
- **Story flags:** sFlagManager (global 0xdcc014, size 0x144, vtable 0xcd9184, DTI 0xdcff60) holds the script flags as 0x47 dwords at +0x20. Script opcodes (table of 12-byte rows {name, handler, signature} around 0xcd5990): FlagSet 0x577830 calls set(index, count, value) 0x59c980 (thiscall, ret 0xC); FlagSetMB 0x5778d0 goes through 0x59c960; FlagSetF / SetEnemyFlag 0x577970 through 0x59c320; SetLocalFlag 0x577f50 uses the singleton at 0xdcbebc. flag_sync diffs the 0x47 words (FLAG_DIFF 0x010C).
- **Room phases:** sRoomControl (0xdcbeb4) +0xb8 is the phase manager (vtable 0xce6bdc): +0x8 count 0x18, +0xc array of phase objects, +0x14 current, +0x18 requested (-1 none), request(phase) 0x60a340 (thiscall, ret 4; sRoomControl::requestPhase 0x610e00 adds 0xb8). Index: 0 Init, 1 Main, 2 Message, 3 MessageImm, 4 DoorLoad, 5 SubScreen, 6 Save, 7 UpCut, 8 Option, 9 Change, 10 EventDemo, 11 Map, 12 Movie, 13 Event, 14 Dead, 15 Opening, 16 StaffRoll, 17 Ranking, 18 WeskerTitle, 19 WeskerRanking, 20 OmakeTitle, 21 OmakeResult, 22 PlayDemo, 23 Exit (read from the live array, 2026-09-29). Class vtables: cRoomPhaseMain 0xce6b90, Dead 0xce68a8, EventDemo 0xce6a20, Movie 0xce6ea0.
- **Zapping (V):** the vanilla V requests room phase Change (9) through sRoomControl::requestPhase 0x610e00 from the player think (0x4fed48 when the target is the partner, 0x50395e); the Change phase does the swap and loads the partner's room when apart. camera_parity swaps directly in the same room and requests Change when the partner is in another room (untested in game).
- **Save manager / slots:** singleton 0xdcc018 (+0x20 request object; dispatch 0x6133d0 on op +4: 1 0x612ad0, 2 0x612c40, 3/4 0x613080 load, 5 0x612e30, 6 0x612d20). Load request (slot, 0) thiscall ret 8: op 3 0x6134c0, op 4 0x613500 (the load menu calls them through 0x613cb0/0x613cc0 with its cursor index); save request (slot) ret 4: op 5 0x613390 (via 0x613c40, called from the save GUI). Slots 0..19 are player saves; slot 21 (0x15) is the system data read at boot ("Load successful."). From boot, Enter on every screen reaches the game through the notice, title (Load Game default), load list and Continue.
- **Game over:** Main -> Dead (14) phase; uGUIGameOver vtable 0xcdb2f0, sGameOver 0xcdb288. Setting HP to 0 does not kill; death only comes through the damage path. Writing the phase manager's request field directly crashed the game (use requestPhase). The co-op flow does not read the game over menu: the guest waits muted and continues (Enter) once the host is in game again, assumed to be the default choice (unverified).
- **Save sharing:** the host's typewriter save goes to the Steam cloud through the remote-storage proxy; a successful write sends SAVE_CHANGED to the launcher, which re-sends data0.bin (2.3 MB) to the guests, so a continue after a game over loads the same state everywhere.
- **Submenu open:** sSubMenu::open 0x5d9030 (thiscall, no args) resets the menu and sets state +0x2c = 0; it runs from 0x60bef0 and shows the focused character's inventory. States then step 1..6 while open, 7/0xb/0xc on the way out, 0x0d closed.
- **door_sync:** hooks sDoorLoad::start 0x552b50 (thiscall (room, entry, a3, a4, flag), ret 0x14; stores +0x20/+0x24/+0x34/+0x38, resets the phase +0x44 and switches sRoomControl to the door phase; callers are the door script request 0x552ae0 and two other script paths). The owner of the focused character runs it and sends DOOR_CHANGE 0x010B {room, entry, a3, a4, flag, u8 character}; the other machine suppresses its own starts and runs the received one on the game thread once no door is active, so it plays the door and loads the room normally. Calling changeRoom 0x610c60 directly skips the door animation and leaves the room half-loaded (room reads 0xffff until a camera cut). F8: doors sent/run/blocked.
- **Only the focused character can act on doors:** door (and other interaction) scripts use the condition 0x564070 (cdecl bool(void*, ctx*)): controlled player +0x16e4 (trigger zone; also kept for the partner, -1 outside any zone) == ctx +8, getPad(0) action pressed (pad vtable +0x48), sPlayer 0x4ec1e0(7) == 0. The door script then calls sDoorLoad::request 0x552ae0 (from script opcode 0x579010); sDoorLoad (vtable 0xcd3120, update 0x552300) counts down +0x2c and calls the thunk 0x552720 -> changeRoom. door_sync runs the check with sPlayer +0x2c temporarily set to the local partner when the peer owns the focused character; when it passes, focus moves to the local character first, and DOOR_CHANGE carries that character id so the peer focuses it too.
- **In current room:** player +0xc attribute flags, bit 0x4000 set while the character is in the loaded room (clear for a partner left elsewhere).
- **Current stage/room:** sGameInfo (global 0xdcbe9c) +0x2a80 stage, +0x2a84 room; the record at +0x2a88 keeps the previous room in its first dword. uScrDisp (recreated per room) also has mStageNo +0x30, mRoomNo +0x34, mReqRoom +0x44 (from MT property registration at 0x63a950).
- **Door transition:** sDoorLoad (global 0xdcbeb8) +0x44 (i32): 0 → 1 → 2 → 4 during the transition, 5 idle afterwards; after a fresh save load the idle value is 0xFFFFFFFF (-1), so a door is running only while the phase is in 0..4 (`door_phase.h`). F8 panel lines: door phase (raw), room (hex).
- **Inventory / pause menu:** sSubMenu (global 0xdcebd0) +0x2c: 0x0d closed; other values while a submenu or the pause screen runs.
- sGamePause (0xdcbea8) +0x24 is only a focus-loss marker; setting it does not pause the game.
- **Adapter door/menu behaviour:** `door_travel` latches the door start (game tick and net thread, level-triggered) with whether the partner had the in-room flag. The partner is not moved by the adapter: writing partner +0xff4 (room object) and position makes it visible but leaves the game's room membership half-updated (E command refused, crash when the partner later walks through a door). Instead party_mode mirrors TEAM/LEAVE_BEHIND into the game's own follow flag, sPlayer +0x40 (u8, 1 = partner follows; the vanilla E toggles it), so the vanilla door logic carries a following partner. No partner AI is given at door start; partner_think skips applying while the door runs and for 30 frames after (polled every tick from `game_state::doorActive`). Arrival sends ROOM_STATE {u16 room, u8 partnerInRoom, u8 pad} and forces one position snap. PLAYER_STATE carries `room` in the former reserved bytes; corrections only apply when the rooms match. Remote-owned characters are corrected continuously after their own move: drift under 3 is ignored, up to 60 moves 0.25 of the way (position and rotation) toward the owner's newest state extrapolated by velocity (capped 100 ms), beyond 60 snaps. `menu_mirror` sends MENU_STATE {u8 open}.
- **Save:** the full data0.bin image is assembled in memory and written by 0x6133d0 → 0x612ad0 → 0x8b3280 → 0x8b3940 → ISteamRemoteStorage::FileWrite("data0.bin", buffer, 0x23a8f0). The step that serializes the live game into a slot (typewriter) is upstream and not found yet.
- Other statics: sRoomControl 0xdcbeb4, sLoading 0xdcf688. MT property names ("mRoomNo", "mPause"…) point at registration code that gives exact offsets.
- **D3D9 device:** hook d3d9!Direct3DCreate9 inline before the decryption wait; the game creates its device within about 1 s of start. (0xdc0790 is not a reliable device pointer.)

## Unit update dispatcher and world freeze (2026-09-29, static disassembly)
- `sUnit::updateAll` **0x727b50**: thiscall, no args, plain `ret` (vtable 0xd1bfd8 slot 6, also referenced by vtable 0xcf3bf8). Once per frame, called from 0x67ce00 (`call [eax+0x18]`) via 0x72373c. Wrapped in a scope object (0xa3b4e0/0xa3b510), loops groups `i < [0xe2d434]+0x620` calling slot 9.
- Slot 9 **0x7279e0**: thiscall, 1 stack arg (group index), `ret 4`. This is the function containing return address 0x727b0e. Per unit in the group's list (+0x2c, next at +0x14): state 1 runs unit slot 5 (init) and becomes 2; state 2 with flag 0x400 calls unit slot 8 (+0x20), the move chain (player slot 8 0x526b30 leads to `uPlayerBase::move` 0x513190 through slot 41 0x51b680). Units flagged in the group's +0x28 bit 0 are queued on a job list at [0xe2d904]+0xac instead. Slot 10 (0x727bb0, run per frame by slot 11 0x727ce0) is a second phase: after init it calls unit slots 8 and 10 once, then unit slot 9 (+0x24) each frame.
- Neither function touches a pad, input or a render call: they only walk unit lists. The engine itself early-outs both when `[0xe2d904]+0x60 == 0 && byte [0xe2d904]+0x64 != 0` (its own pause), so skipping the whole call is a state the engine already supports.
- Adapter freeze: `menu_mirror` hooks 0x727b50 (once per frame, no args) and returns without calling the original while a peer's menu is open and ours is closed. Rendering, input polling and the net thread keep running. Side effects: `game_tick` callbacks (run from the controlled player's move) do not fire while frozen, so the freeze state is evaluated in the detour itself and MENU_STATE is sent from the net thread. The second phase (unit slot 9, e.g. animation/motion) is not hooked and keeps running, so frozen characters may still animate.
- Untested in game: whether the vanilla pause menu itself stops calling 0x727b50; door state values seen only at the ends of a transition when the callback stops firing.

## Items (2026-09-29)
- sItem* 0xdcbf44 holds inventories as fixed per-character blocks (unchanged by the V switch): **Rebecca at +0x24, Billy at +0x64**, 0x40 bytes each = 6 slots of {u32 itemId, u32 count} plus 0x10 of extras (third extra dword = the equipped slot index). 0xb4 marks the second half of a 2-slot item. Seen ids: 0x68 Hunting Gun, 4 Handgun, 0x20 Handgun Bullets, 2 Knife, 0x2d herb.
- sItem slot writers: set 0x4dae04/0x4dae8b, remove 0x4db2b9, add on pickup 0x4dc269. The drop (Leave) runs through the player's update: 0x513570 → 0x500db2 → 0x50e1c4 → 0x5d763e → 0x4dc750.
- sItemPut* 0xdce0a8 = the dropped floor items. Layout (static, from the ctor 0x4de150, dtor 0x4de1f0, DTI 0xdce9bc, vtable 0xcc7bfc, getDTI = slot 4 0x4df910): the object holds **28 records of 0x24 bytes at +0x20** (ctor loops 0x1c+1 times calling the record ctor 0x4de0c0; initial record = all zero except the dword at +0x14 = -1). A cleared record seen at runtime: {u32 room?, ..., u32 itemId, u32 count, u16/u16 position-ish, i16 -1, i16 -1, u8}. Field meaning is NOT confirmed statically, and there is no used flag found: the adapter treats the 1008-byte array as plain data and compares it whole. The names mUseItemCount/mUseItemHead/mEmptyItemCount/mEmptyItemHead belong to a different class (property list at 0x55cb40, xref of "mpStageEffectManager"), not sItemPut; sItemPut's own slot 3 (0x608180) is generic. Level-placed items live elsewhere (0x4dfa00 reads a stage item table, keyed by 0x71/0x73 kinds; pickup flags not found yet).
- Item sync (adapter): INVENTORY 0x0106 `{u8 characterId, u8 block[0x40]}` reliable, sent by the owner of a character when its sItem block changed and stayed unchanged for 10 frames, and every 5 s; the receiver overwrites the block only if it treats that character as remote-owned. Floor items are synced by event, see below.

- **sItemPut record layout (runtime-checked 2026-09-29):** 28 records of 0x24 bytes at object +0x20: {u32 key, u32 0, u32 itemId, u32 count, **u32 pointer to the spawned uItem object**, u32 0, u32 0xff, u32 0, u32 0}; an empty record has -1 at +0x14. Because of the live pointer, whole-record overwrites are unsafe. Whole-record overwrites are unsafe, so floor sync replays the game's own put/remove instead.

- **Floor put/remove (static analysis 2026-09-29, untested in game):** `sItemPut::put` 0x4de500, thiscall, ret 0xC, `this` = sItemPut* (0xdce0a8; the caller gets it as `0x404fe0(0x480460(&tmp))`, a weak-pointer accessor chain). Args: (1) `ItemDesc*` {u32 itemId, u32 count, u32 0}; (2) **position** vec3, copied to the new uItem at +0x40; (3) **Euler rotation** vec3, passed (y minus a constant) to 0x719f20, which builds the unit's rotation. The Leave caller (0x5e4b84 in 0x5e1d10) builds arg 3 with 0x67fcc0 = matrix to Euler angles (`this` = a 4x4 built from the player's facing at 0x5e4a63, output pointer returned), so it depends only on the dropper's facing; the adapter reproduces it by forwarding the rotation the local put received. put also reads `sPlayer->controlled + 0xff4` (must be non-null) and returns the uItem. `sItemPut::remove` 0x4de730, `ret 4`, one arg (the uItem*); it loads `this` from [0xdcbf40] itself, so ecx is ignored. Adapter (`floor_items_sync`): both are MinHooked. A local call (not while applying a network event) runs the original and then sends FLOOR_PUT 0x0107 `{u16 room, u16 0, u32 itemId, u32 count, f32 pos[3], f32 rot[3]}` or FLOOR_TAKE 0x0108 `{u16 room, u16 0, u32 itemId, f32 pos[3]}` (reliable; the take reads itemId and position from the uItem before the original frees it). Calls made inside a remote-owned character's move scope are suppressed and not broadcast. The receiver (game thread) applies an event when `room` equals our current room and the room is ready (no door running, 30 frames after door arrival, item table and controlled character present); otherwise it goes to a per-room pending queue (`floor_pending`, cap 32 per room, oldest dropped), where a TAKE cancels the nearest matching pending PUT (same itemId within 50 units) instead of being stored. When a room becomes ready its pending events apply in arrival order through the same apply code (reentrancy flag, no re-broadcast). RE0 keeps no plain in-memory table of other rooms' floor items (only the current room's sItemPut; other rooms are presumably in the save image), hence the adapter-side queue. Not covered: a late joiner sees nothing already on the floor, and the queue is lost on restart. F8 panel: floor pending (gauge), pend appl/drop. TAKE finds the local record with the same itemId whose uItem (+0x40) is within 50 units and removes it; no match counts as a take miss. Risks: put may also run when a room loads persisted items (would duplicate for a peer already there); the suppressed remote-move case assumes Leave/pickup run inside the character's move.

- **Pickup action (corrected 2026-09-29):** step 0x500d00 (thiscall, state object, 1 arg player, ret 4; state +0x10 phase 0 start / 1 take / 2 done, +0x18 = the player). The take reads `[player + 0x67a0] + 4` (0x4ded40 is just that accessor), and player+0x67a0 is the interaction target, cleared when the item is removed, with no null check (crash at 0x500da3). The adapter's pickup_guard ends the action (phase 2) when the target is null.

- **Room membership (confirmed in-game 2026-09-29):** player +0xff4 = pointer to the room object the character belongs to. Each frame the player update (0x529990..0x529a1f) sets "hidden reason" bits in +0xfc8 through 0x6534f0(bit, set); reason bit 2 = 0x61b0b0([0xdcbf40], room obj, 1), i.e. "room not loaded". +0xc bit 0x4000 (active/in room) is on only when +0xfc8 & ~1 == 0. **Setting a partner's +0xff4 to the focused character's value and writing its position (+0x40) brings it into the current room** (tested: reasons 0x4 -> 0, the partner appears).

## Keyboard (config.ini)
W/S/A/D move, Space run/attack, Shift aim, F action, **V switch character**, E partner follow toggle, arrows move the partner, Q map, C status. **Space cancels menus**; Esc opens pause (Quit Game is on that menu).

## Co-op design direction
Each client controls its own character. On each machine, the remote player is the partner. The partner is driven by network state; replacing its think vs mirroring its state is decided after in-process tracing.

## Party command path (revised 2026-09-29)
E/V are polled by `command_input::onNetTick` on the net thread (~5 ms), not from a game_tick callback and not from the render path: the net loop keeps running through menus, doors and the world freeze, so the raw-key edge detector never stalls (a hook-driven detector missed edges whenever move() stopped). A press logs `command: <name> pressed (VK, foreground)`; it becomes a request only with the game window in front and a peer connected. Host and guest presses share one request path per command (`camera_parity::queueSwitch`, `party_mode::queueToggle`; a guest's SWITCH_REQUEST / PARTY_REQUEST calls the same function); the host decides on the game thread and logs applied or ignored with the reason (door active, focus already set, no partner object, partner is not the target, no peer connected). Panel: last command and last decision with age. `key_config` reads the first `KC_change`/`KC_trace` line in any section whose value is a known `KB_` name.

## Separate rooms study (2026-09-30, unmodified game, solo)
Goal: both players in different rooms at once (each machine loads its own player's room). Findings so far:
- Leaving the partner behind (E = stay, controlled character takes a door) clears the partner's in-room bit
  (+0xc 0x4000); the game keeps simulating only the loaded room.
- Each character has an `sCollision::SbcInfo` at +0x1870 (vtable 0xcc3fb4): +0x1880 stage, +0x1884 room of the
  collision set it uses. The left-behind partner keeps its old room there (2, 3 in the test).
- **SbcInfo is not the partner's room record.** Rewriting the partner's SbcInfo room and position, then reloading
  rooms, changed nothing: on return the game placed the partner back in its real room at a position of its own
  (241, 600, 2852) and set the in-room bit. A separate partner room/position record exists.
- Current-room record: sGameInfo +0x2a7c {entry, stage, room, previous room}; +0x2a90..+0x2a9c gets a copy once
  the characters split (0xffff while together). Candidate for the partner's room: sGameInfo +0x1e5c (3 in both
  snapshots, the room the partner stayed in); needs a third sample with the partner in a different room.
- Controls in this setup are camera-relative (D moves right on screen), not tank controls.

What separate rooms needs, in order:
1. The partner room/position record (write it when the remote player changes rooms, so a reunion places them
   correctly through the game's own logic).
2. Camera parity off while apart: each machine focuses its own player's character.
3. door_sync only between players in the same room (already the TEAM rule) and no door forcing while apart.
4. Enemy authority per room: whoever is in a room simulates its enemies; host authority only when together.

**Scene records (static analysis of the door carry 0x61e2c0, confirmed in game 2026-09-30).** sSceneInfo (0xdcbf40)
keeps up to 4 room records (pointers at +0x2dc70); +0x20 is the loaded one. A character belongs to the record at its
+0xff4; a room a character was left in stays as a dormant record (that is the "hidden" partner room: record +8 = scene
id, the same id doors pass, e.g. 0x59, 0x61, 0x24; +0x9aa4 = entry spots, 0x18 bytes per (entry * 3 + mode)). The carry
moves a character with: [0xdcc010]->0x411570(-1, player +0xff0) (unit registry, detach); 0x61e0f0(scene, flags)
on sSceneInfo (find the room's record, or load it dormant; flags = [0xdd1d44] | 4); 0x61ed50(player, entry, mode) on
the record (entry spot, mode 0 = through the door, 2 = following partner); 0x619e30(record, player) on sSceneInfo (leave
and enter callbacks, +0xff4); 0x411570(scene, handle) again; 0x61dde0(record) releases an empty dormant record.
`scene::move` runs exactly that for one character. Tested with the fake session: Billy sent from the loaded 0x59 to
0x24 vanished with no screen change; sent back he stood at the door's spot (241, 600, 2852, where the game itself puts
him); moved away (0x59 released) and back (0x59 loaded fresh, same slot reused), then Rebecca walked into 0x59 through
its door and found him there. A rejoining guest took Billy through his door into 0x61 and Rebecca was moved into
dormant 0x24; the fake host's Rebecca then walked into 0x61 and appeared at the door. The in-room flag (+0xc 0x4000)
of a character moved into a dormant record can stay set (it only updates while the character's room runs), so "in the
loaded room" also compares +0xff4 with the loaded record.

So split rooms needs no replay: a peer's door moves the peer's character in place (split_rooms.cpp, always on), the
save and later doors see the engine's own state. LEAVE_BEHIND is independent play (own camera on each machine, V does
nothing). Enemies: the machine that was in a room first keeps simulating it when the other walks in (ROOM_STATE carries
the claim; host on a tie). Cutscenes that move the peer's character hand the result to its owner (event_place).

**The game's own switch (V)** is a multi-step action in the player think (0x4fec64..: states 3/4/5): in the same room it
swaps directly (no room phase), apart it requests Change from 0x4fed48 / 0x50395e. Refusing requestPhase(Change) misses
the first path, so the switch is cut at its input: the DirectInput keyboard never reports the KC_change key during co-op
(virtual_keys), which also keeps it out of the pad frames the peer replays. A gamepad's switch button is undone by
camera_parity's keepOwnFocus in independent play.

**Boot-time save:** on the boot notice screen the game issues its own save request for slot 0 (no room phase). Redirecting
it into the co-op slot made the next load assert in the scene id check (crash at 0x401f78 via 0x610d8a); session_slot now
redirects only saves made from the Save room phase.
