# RE0 scripted events: design (2026-10-07)

Live reports (2026-10-07), one cause: a room script event runs only on the machine whose game fired it.
- The host picked up a key item; the event fired on the host (crows spawned and broke the window), the guest saw
  nothing and had no crows.
- Placing a quest item (using a key item on a slot or statue) changed the world and the puzzle on that machine only.
- The guest (Billy) worked the train's lift: the cutscene played on the guest only and the host's Rebecca never went
  up (in the vanilla game Billy works the lift and only Rebecca rides it, which splits the party).

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

## 2. Design: the subject's owner leads, the other machine replays the same thread

A thread runs on both machines. Its **subject** is the character the script treats as the player: the firer's own
character, until a CharChange op makes the other character the player. The subject's owner **leads** the thread (runs
it natively and decides every branch); the other machine **follows** it (runs the same ops in the leader's order and
takes the leader's branches). Rule behind it: whatever the script does to a character, and every choice made on a
player's screen, is done by that character's owner; everything else runs on both machines, once each.

1. **Who may fire.** The scan's start passes through a hook on startThreadOnce.
   - *Local* conditions (only the controlled character) fire on the machine whose own character satisfies them. Each
     machine keeps its own character controlled (camera_parity), so the peer's copy never satisfies them there.
   - *Shared* conditions (flags, enemies, units, timers, either character) fire only on the room's authority, the
     machine that runs the room's enemies (`door_travel::enemyAuthority`, the same claim enemy sync uses), while the
     peer is in the room; the other machine's start is refused. A player alone in a room fires everything (vanilla).
   - Item use from the inventory (return 0x568193) is local: the user's character met it. The item-use probe (return
     0x567fa1, which runs a thread's first ops and ends it) is left alone: it is not an event.
2. **Leader.** A thread it starts (scan or item use) is tracked under a serial, and EVENT_START (0x0130) goes to the peer
   when the peer is in the same room: serial, key, scene, trigger index, the pc to start at, the condition type, the
   subject and whether the partner still follows. The op dispatch hook records each finished op (pc moved, or the
   thread ended) as a step `{serial, from pc, to pc, result}`; the steps go out once per frame as EVENT_STEPS (0x0131).
   A fork (EventExec) is tracked as its own thread with its own start. A thread that disappears without ending (room
   reset) sends a kill step. Both messages are reliable on one channel, so a start always arrives before its steps.
3. **Follower.** A start is applied at the top of the peer's sEventScript update when the peer has the same scene
   loaded: startThreadOnce(key, index, type) (startThread(key, pc) for a fork), then the pc is set to the start pc. In
   the dispatch hook a follower runs an op only after the leader finished it:
   - no step from this pc yet: yield (the op is not called);
   - *leader-only* ops are not run, the pc goes to the leader's `to`: messages and selections (mes*, nomes*,
     mes_dialog), close-ups (UpCut*: the slot or panel the player is looking at, and the choice made there), the
     inventory and item screens (item_*, key_check, Sub*, SubItem*, UseKey, ItemPut), doors (door_sync carries them),
     the typewriter, equips (inventory_sync and equip_refresh carry them), and EventExec (the fork arrives as its own
     start);
   - pure waits the leader already finished are skipped the same way (wait, frame, sewait, anim_WaitType,
     Fade*_Wait, EventProcWait, UpCutEndWait, wait_movie, wait_cancel), so the follower stays one network delay
     behind without adding its own waits;
   - every other op runs with the subject as the controlled character (sPlayer +0x2c and +0x3c swapped for that call
     only): story, enemy, effect and sound flags through the game's own setters (enemy groups spawn, props change
     their look), sounds, camera cuts, fades, object animations, cutscenes and movies, player animations and positions
     (on the subject), ControlPause and the partner pause (this player's own character, as in single player). When
     the op finishes, the pc is set to the leader's `to` whatever the local branch was; an op that has not finished
     locally 3 s after the leader finished it is ended there (logged);
   - the step's result is returned, so the thread ends, yields or continues exactly where the leader's did.
4. **Character switch (CharChange, CharChangeNC, CharChange2).** Run on neither machine: each player keeps their own
   character and camera. Both machines switch the thread's subject to the other character at that op, and the lead
   passes to its owner: the old leader records the step and becomes the follower, the old follower takes the step and
   leads from there (the serial stays; every message says whose serial it is).
5. **Partner follow (TraceOff, TraceOn).** Run on neither machine (party_mode owns the game's follow flag). The thread
   notes that its partner stays (or follows again), and the host, whichever role its copy of the thread has, sets the
   party mode to Leave behind (Team) and announces it. A door op of a thread whose partner stays is sent as a door
   that leaves the partner behind (DOOR_CHANGE flag `kDoorAlone`): the peer never goes along, whatever the party mode
   says at that moment.
6. **Both fired.** If a start arrives for a thread this machine already runs as its own (both players met a local
   condition in the same moment, typically a room-entry trigger after a door taken together), both keep their own
   thread and ignore the other's steps; it is logged. Shared conditions cannot race (one authority).

## 3. Quest item placement

Using a key item on a puzzle spot is a room thread started from the inventory's item use (or an examine whose script
opens the item selection, `Sub` / `Sub_GetItem`): train room tr0a trigger 5 (type 8, item 0x4d in zone 3) runs
`SubItemJp`, a close-up (`UpCutStart`, `UpCutWorkSet`), `SetEffectFlag 0,0 / 1,1`, `FlagSet 0x70`, `item_sub 0x4d`, the
sound, a message and the panel routine, then `SetEffectFlag 4,0 / 5,1`. The user leads it:
- the item leaves the inventory once: `item_sub` runs on the user's machine only (leader-only) and inventory_sync
  carries the block to the peer (its owner is the user);
- the prop's look: effect flags (0x712 + n, sGameChara's effect setter), object animations (`anim_set*`, `obj_move`)
  and visibility ops run on both, so the slot shows the item and the mechanism moves on both screens; a room loaded
  later builds it from the flags;
- the script that follows (flags, spawns, cutscene, a door it opens) runs on both as above;
- the close-up of the slot and the message stay on the user's screen.
The item-use thread runs under the scene record's script key (record +0x9914, set to the scene id when the record is
made, 0x61e2a4), so the follower starts it the same way. Not covered: props whose state lives only in the unit and is
changed by a player's body rather than by a script (a block pushed by hand); their result reaches the peer only through
the flags the script sets when the puzzle completes.

## 4. Lifts and other scripted room changes

The train's lift (tr0a): trigger 3 (examine zone 2) puts the examining character into the lift (`PlayerPause`, the
lift demo by character (`PlayerCheck`), `ChPosSet` of the player, `CorrectPos2` of the partner, `PlayerEventStart 1`,
`TraceOff`, `FlagSet 0x6f`); trigger 4 (the lift's panel, zone 3) is a close-up with a choice (`UpCutProc`), then for
"up" `FlagSet 0x71`, the lift cutscene (`Demo_Start 0x12 / 0x11`), `CharChange2` (the player becomes the character in
the lift), a fade and `ControlContinue`. In co-op:
- Rebecca's player examines the lift: that machine leads trigger 3, Rebecca is placed in the lift on both machines,
  and TraceOff makes the host set the party mode to Leave behind (the split the game intends);
- Billy's player works the panel: that machine leads trigger 4 (the close-up and the choice stay on its screen), the
  lift cutscene plays on both, and at `CharChange2` the lead passes to Rebecca's player, whose machine runs the rest
  (fade, weapon display, ControlContinue) with Rebecca as the player; neither camera switches;
- Rebecca then leaves the lift upstairs through its own door trigger on her player's machine, alone (Leave behind).
Generally a script that switches the player and then sends that character through a door (`CharChange2; TraceOff;
door`, e.g. dm0b trigger 5) has its door run by that character's owner, and the door leaves the partner behind.
Read from the scripts only; how Rebecca leaves the lift upstairs (a door trigger gated by flag 0x71) is inferred.

## 5. Cases

| Case | Behaviour |
|---|---|
| Peer in another room (split) | No start is sent; the leader's flags reach the peer through flag_sync. When the peer later enters, the guard flags are set, so the event does not replay, and the room load spawns what the enemy flags say. |
| Peer arrives mid-thread | On the arrival (peer place becomes "here") the leader sends a start for every led thread still running, with its current pc; the follower jumps there and follows from that op. A thread whose subject is the arriving player's character is handed to them with that start. |
| Start for a room the follower is not in | Kept while the follower is loading or in a door (up to 10 s), dropped otherwise; steps for an unknown serial are ignored. |
| Leader leaves the room | Its room reset ends its threads; the kill steps end the followers. A shared trigger that still holds then fires on the remaining player, who is now alone and the authority. |
| Peer disconnects or leaves the game | Followers are released: they continue natively from their pc (a cutscene still reaches its ControlContinue). |
| Cutscenes | The follower runs the same EventPhaseIn / Demo_Start / PlayMovie ops one network delay later, so both machines show the cutscene; menu_mirror holds the follower's world for that delay (EventDemo and Movie hold) and stops holding once the follower is in the cutscene too. event_place keeps handing character placements to their owners. |
| Double firing | A shared trigger fires once (authority only). A local trigger fires on the machine whose character met it; the follower's own scan cannot start it again while it runs (startThreadOnce), and the replayed guard flags stop it afterwards. |
| Enemies spawned by an event | Spawned on both machines by the same `SetEnemyFlag` ops in the same order (despawns and EnemyDeath ops too), each through sEnemy's create, which enemy_spawn seeds from the spawn record, so pool slots and variants match; the room's enemy owner stays as it was. |

## 6. Wire

```
EVENT_START 0x0130 reliable, 16 bytes:
  u32 key, u16 scene, u16 serial, u16 index (0xffff fork), u16 pc, u8 type (0xff fork), u8 subject character,
  u8 kind, u8 flags (1 = the serial is the receiver's, 2 = the partner follows)
EVENT_STEPS 0x0131 reliable: u16 count, u16 pad, then count x 8 bytes:
  u16 serial, u16 from, u16 to, u8 result (0 yield, 1 next, 2 end, 3 killed), u8 flags (1 = the receiver's serial)
DOOR_CHANGE 0x010B: the former first pad byte is now flags (1 = a room script left the partner behind)
```

## 7. Logs for the next live session

- leader: `event_sync: fired scene 0x0a trigger 4 (type 0x02, local) as Billy at pc 0x2d3, serial 12, peer follows`
- refused: `event_sync: scene 0x07 trigger 6 (type 0x0e) is the room authority's to fire, not fired here` (once per
  trigger and room)
- follower: `event_sync: following scene 0x0a trigger 4 (type 0x02, local) serial 12 led by Billy on the peer, from pc 0x2d3`
- lead change: `event_sync: serial 12 (scene 0x0a trigger 4) switched to Rebecca, its player leads from here` on one
  machine and `..., leading it here` on the other; `party mode set by a room script` in the command log
- follower end: `event_sync: followed serial 12 ... ended: 23 ops run, 4 leader-only skipped, 2 waits skipped, 1
  branches taken from the leader, 0 forced, 0 jumps, lag up to 85 ms`
- doors: `door_sync: door to room 0x.. entry .. sent, partner left behind`
- both fired / dropped / released lines; F8 panel: events fired / followed / refused / dropped.
Check live: the crows (or any key-item event) on both screens with the crows alive on both; placing a quest item (the
slot changes on both, the item leaves the placer's inventory once); the train lift (both see the cutscene, Rebecca goes
up, Billy stays, the party becomes Split up); a door event in Team and Leave behind; a room entry together; one player
examining a typewriter (no message on the other screen).

## 8. Risks

- The perspective swap covers the getters of sPlayer +0x2c / +0x3c; code that reads sGameChara's mirrors
  (+0x148c / +0x149c) during an op would still see this machine's own character.
- An op that does its work over several calls (not a pure wait) starts on the follower only after the leader
  finished it; a long one (a cutscene that loads) can put the follower behind by its own duration.
- Both firing a local trigger at once runs the body twice (two item_get of the same scripted item is possible, rare).
- The leader-only, wait, switch and follow lists are by opcode (`event_rule.h`); an op misfiled there would run on one
  machine only, or on both.
- CharChange* is taken as "the other character becomes the player" for all three variants; CharChange2 was read
  (swap, and load the new player's room when apart), the other two were not.
- A script's TraceOn sets the party mode back to Team.
