# RE0 scripted events: design (2026-10-07)

Bug (live, 2026-10-07): the host picked up a key item, the room's event fired on the host (crows spawned and broke the
window), the guest saw nothing and had no crows. A room event runs only on the machine whose game fired it.

Goal: when a room script event fires on one machine, the same event runs on the other machine too: the same scripted
actions and branches, the same spawned enemies, the same cutscene, in step, run once, with one writer per piece of state.

## 1. How the game runs room scripts (static, image.bin and nativePC/event/*.bes2)

| Piece | Engine | Notes |
|---|---|---|
| Script file | `nativePC/event/<stage><room>.bes2` (rEventScript), big-endian | u32 trigger count, 12 bytes pad, per trigger `{u32 condition type, u32 code offset, u32 p1, u32 p2}`, then the code; offsets are from the file start. `tools/re0/bes2.py` disassembles one. |
| Opcode table | rows `{handler, signature, name}` of 12 bytes at **0xcd57d8**, 312 rows | an opcode is a big-endian u16 index; operands follow, sized by the signature (U1 1, U2/S2/Fn 2, U4/S4/F4 4, Rn 1; sizer 0x57cc10). The earlier notes read the rows one field off (FlagSet is 0x577790, not 0x577830); corrected in RE0_NOTES. |
| Script manager | sEventScript, global **0xdcbebc** | 8 thread slots of 0x88 bytes at +0x114: +0 active, +4 script key, +8 code base, +0xc pc, +0x10 timer, +0x34 trigger index (u16, 0xffff for a fork), +0x36 flags, +0x38 op sub-state, +0x84 step count. Script resources by key in the list at +0x554. |
| Trigger scan | **0x568270** (thiscall, key; ret 4), from cRoomPhaseMain's update 0x608ad0 (call 0x6097e7) every frame, key = loaded scene id (scene record +8) | for each trigger i: `cond = table[type](key, entry)` (condition table **0xd97368**, 45 types); true starts the thread. Only the Main phase scans. |
| Thread start | `startThreadOnce` **0x57f800** (thiscall (key, index, type); ret 0xc; returns the thread or 0) | 0 when a thread of (key, index) already runs. Callers: the scan (return 0x568530), the inventory's item use 0x568080 (return 0x568193) and the item-use probe 0x567e90 (return 0x567fa1, which runs the thread's first ops and kills it at once: a "can this item be used here" test). Forks: EventExec 0x5714c0 calls startThread 0x57f7e0 (key, pc). |
| Thread run | sEventScript update **0x5835a0** (thiscall, no args), from the Main phase right after the scan and from three other phase updates | per active slot, op dispatch **0x57ce80** (thiscall (thread); ret 4) calls the opcode's handler: 1 = run the next op now, 0 = yield to the next frame, 2 = thread ends (0x581b90 ends a thread). An op that waits returns 0 with the pc unchanged; a finished op moves the pc (some, like `frame`, move it and yield). |
| Condition types | 0xd97368[type] | Read the controlled character only (sPlayer +0x2c: zone, pad, inventory, entry the room was entered by, the item being used): 1, 2, 6, 7, 8, 9, 10, 22, 30, 31, and 4/5 with p2 = 0x20000. The rest read flags, enemies or other units, timers, or both characters (14: controlled or partner in zone). |

Every script guards its body with story flags: `ResFlag G; if G == 0 { FlagSet G, 1; ...body... }`. A crows event,
most likely train room tr07 (trigger 6, type 14 = either character in zone 6; inferred from the script, the live
report did not name the room): `FlagSet 0x43; SetEnemyFlag 1, 1` (spawns enemy
group 1), the glass sound `se 0x365`, `SetEnemyFlag 2, 1`, `ControlPause`, a camera cut for 30 frames, `ControlContinue`.

Why the partner saw nothing: the condition is evaluated on each machine against its own controlled character, so only
the firing machine starts the thread; flag_sync then copies the guard flag G to the peer as a raw bit, so even when the
peer's own trigger later becomes true its thread finds G set and skips the body; and the enemy flags arrive as raw bits,
which spawn nothing (setting an enemy flag spawns its group only through the game's setter 0x59c980 -> 0x4109f0).

## 2. Design: the firer decides, the peer replays the same thread

One machine fires each thread (the **firer**); the other runs the same thread as a **follower** that executes the
same ops in the firer's order and takes every branch the firer took.

1. **Who may fire.** The scan's start passes through a hook on startThreadOnce.
   - *Local* conditions (only the controlled character) fire on the machine whose own character satisfies them. Each
     machine keeps its own character controlled (camera_parity), so the peer's copy never satisfies them there.
   - *Shared* conditions (flags, enemies, units, timers, either character) fire only on the room's authority, the
     machine that runs the room's enemies (`door_travel::enemyAuthority`, the same claim enemy sync uses), while the
     peer is in the room; the other machine's start is refused. A player alone in a room fires everything (vanilla).
   - The item-use probe (return 0x567fa1) is left alone: it is not an event.
2. **Firer.** A thread it starts (scan or item use) is tracked under a session serial, and EVENT_START (0x0130) goes
   to the peer when the peer is in the same room: serial, key, scene, trigger index, the pc to start at, the
   condition type and the firer's own character. The op dispatch hook records each finished op of a tracked thread
   (pc moved, or the thread ended) as a step `{serial, from pc, to pc, result}`; the steps go out once per frame as
   EVENT_STEPS (0x0131). A fork (EventExec) is tracked as its own thread and gets its own EVENT_START. A thread that
   disappears without ending (room reset) sends a kill step. Both messages are reliable on one channel, so a start
   always arrives before its steps.
3. **Follower.** A start is applied at the top of the peer's sEventScript update when the peer has the same scene
   loaded: startThreadOnce(key, index, type) (startThread(key, pc) for a fork), then the pc is set to the start pc. In
   the dispatch hook a follower runs an op only after the firer finished it:
   - no step from this pc yet: yield (the op is not called);
   - firer-only ops are not run, the pc goes to the firer's `to`: messages and selections (mes*, nomes*, mes_dialog),
     the inventory and item screens (item_*, key_check, Sub*, SubItem*, UseKey, ItemPut), doors (door_sync runs them
     by the party rules), the typewriter, the character switch and follow toggles (camera_parity, party_mode), equips
     (inventory and equip_refresh own them), and EventExec (the fork arrives as its own start);
   - pure waits the firer already finished are skipped the same way (wait, frame, sewait, anim_WaitType, Fade*_Wait,
     EventProcWait, UpCutEndWait, wait_movie, wait_cancel), so the follower stays one network delay behind, it does not
     add its own waits on top;
   - every other op runs with the firer's character as the controlled one (sPlayer +0x2c and +0x3c swapped for that
     call only): story, enemy, effect and sound flags through the game's own setters (enemy groups spawn here),
     sounds, camera cuts, fades, object animations, close-ups, cutscenes and movies, player animations and positions
     (on the firer's character), ControlPause and the partner pause (this player's own character, as in single
     player). When the op finishes, the pc is set to the firer's `to` whatever the local branch was; an op that
     has not finished locally 3 s after the firer finished it is ended there (logged);
   - the step's result is returned, so the thread ends, yields or continues exactly where the firer's did.
4. **Both fired.** If a start arrives for a thread this machine already runs as its own (both players met a local
   condition in the same moment, typically a room-entry trigger after a door taken together), both keep their own
   thread and ignore the other's steps; it is logged. Shared conditions cannot race (one authority).

## 3. Cases

| Case | Behaviour |
|---|---|
| Peer in another room (split) | No start is sent; the firer's flags reach the peer through flag_sync. When the peer later enters, the guard flags are set, so the event does not replay, and the room load spawns what the enemy flags say. |
| Peer arrives mid-thread | On the arrival (peer place becomes "here") the firer sends a start for every tracked thread still running, with its current pc; the follower jumps there and follows from that op. |
| Start for a room the follower is not in | Kept while the follower is loading or in a door (up to 10 s), dropped otherwise; steps for an unknown serial are ignored. |
| Firer leaves the room | Its room reset ends its threads; the kill steps end the followers (0x581b90). A shared trigger that still holds then fires on the remaining player, who is now alone and the authority. |
| Peer disconnects or leaves the game | Followers are released: they continue natively from their pc (a cutscene still reaches its ControlContinue). |
| Cutscenes | The follower runs the same EventPhaseIn / Demo_Start / PlayMovie / UpCut ops one network delay later, so both machines show the cutscene; menu_mirror holds the follower's world for that delay (EventDemo and Movie hold) and stops holding once the follower is in the cutscene too. event_place keeps handing character placements to their owners. |
| Double firing | A shared trigger fires once (authority only). A local trigger fires on the machine whose character met it; the follower's own scan cannot start it again while it runs (startThreadOnce), and the replayed guard flags stop it afterwards. |
| Enemies spawned by an event | Spawned on both machines by the same `SetEnemyFlag` ops in the same order, each through sEnemy's create, which enemy_spawn already seeds from the spawn record, so pool slots and variants match; the room's enemy owner stays as it was (the authority). |

## 4. Wire

```
EVENT_START 0x0130 reliable, 16 bytes:
  u32 key, u16 scene, u16 serial, u16 index (0xffff fork), u16 pc, u8 type (0xff fork), u8 character, u8 kind, u8 pad
EVENT_STEPS 0x0131 reliable: u16 count, u16 pad, then count x 8 bytes:
  u16 serial, u16 from, u16 to, u8 result (0 yield, 1 next, 2 end, 3 killed), u8 pad
```

## 5. Logs for the next live session

- firer: `event_sync: fired scene 0x25 trigger 6 (type 0x0e, shared) as Rebecca, peer here, serial 12`
- refused: `event_sync: scene 0x25 trigger 6 (type 0x0e) is the authority's, not fired here` (once per trigger and room)
- follower: `event_sync: following scene 0x25 trigger 6 serial 12 fired by Rebecca on the peer, from pc 0x31c`
- follower end: `event_sync: followed serial 12 ended: 23 ops run, 4 firer-only skipped, 2 waits skipped, 1 branch taken from the peer, 0 forced, lag 85 ms`
- both fired / dropped / released lines; F8 panel: events fired / followed / refused.
Check live: the crows (or any key-item event) on both screens with the crows alive on both; a door event in Team and
Leave behind; a cutscene room entry together; one player examining a typewriter (no message on the other screen).

## 6. Risks

- The perspective swap covers the getters of sPlayer +0x2c / +0x3c; code that reads sGameChara's mirrors
  (+0x148c / +0x149c) during an op would still see this machine's own character.
- An op that does its work over several calls (not a pure wait) starts on the follower only after the firer
  finished it; a long one (a cutscene that loads) can put the follower behind by its own duration.
- Both firing a local trigger at once runs the body twice (two item_get of the same scripted item is possible, rare).
- The firer-only and wait lists are by opcode name (`event_rule.h`); an op misfiled there would run on one machine only.
- Script-killed enemies and despawns (`SetEnemyFlag n, 0`) run on both machines; enemy sync is told.
