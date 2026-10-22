# RE0 partner by state replication (pose puppet): gate study

Status (2026-10-19): phase 1 (pose core, echo test) is built on branch `re0-state`; what it does and how to run the echo test: `docs/RE0_NOTES.md` "Pose puppet" and `adapters/re0/README.md`. Phases 2 to 5 below are not started.

Date 2026-10-19. Static analysis only: the decrypted image `coop-framework\tools\re0\image.bin` (build 17178773, base
0x400000), Ghidra 12.1.4 headless on a copy of the analysed project (`coop-scratch\re0-outfit\ghidra`), capstone. The
game was never launched, attached or read. Repo context: worktree `coop-re0-phase4` (branch re0-phase4), CODEMAP.md,
docs/RE0_NOTES.md, docs/re/PATTERNS.md, docs/FINDINGS.md.

Evidence tags used on every claim:
- **[V]** verified in the binary this session (decompiled or disassembled, address given);
- **[N]** from the repo notes (earlier live probes or traces), not re-checked here;
- **[I]** inferred, not proven; the probe that settles it is named in section 6.

Note on the dump: `image.bin` was dumped with the adapter loaded, so `uPlayerBase::move` 0x513190 starts with the
MinHook `jmp`. The original prologue (`55 8B EC 83 E4 | F0 81 EC 24 01`) was restored in memory for the decompile.
No other function looked at here is patched.

---

## Verdict (short)

**Feasible with caveats.** The engine already has a "puppet player" mode: room scripts pause a player's brain with
`PlayerPause` (sets sGameInfo +0x50 bit 1), drive its animation with `Player_AnimSet` (the player's own
`playMotion` with an interpolation length and a start frame), and the per-frame move then runs a motion-only branch:
animation, root motion cancelled, matrices and collision shapes rebuilt, no think, no locomotion, no hit intake.
A per-character puppet is that same branch, scoped to the one `move` call of the remote-owned character.
Every motion request of a character goes through one function (`cAnimControl::set` 0x4144d0, four callers), so the
owner can send the exact arguments and the receiver can replay the identical call. The caveats are the couplings
outside the character: enemies that read and change the player's think state (grabs, damage entry), weapon effects
that live in the fire handler, room scripts that drive the same character, and motion banks that must be loaded.

---

## 1. Animation state on the player object

### 1.1 Layout [V]

`uPlayerBase` (uPlayerRebecca vtable 0xccfd10, uPlayerBilly 0xccf5a0) is a `uModel` subclass. The uModel reflection
registration (0x735f90.., 0x73ddd0..) gives names and offsets:

| offset (player) | field | notes |
|---|---|---|
| +0x1c | f32 time scale | every per-frame delta is `x * [unit+0x1c]` (0x7409b0, move) |
| +0x40 / +0x50 | position vec3 / rotation quaternion | [N] |
| +0x70 | world matrix | rebuilt from +0x40/+0x50 by 0x71ace0 [V] |
| +0x480 / +0x484 / +0x488 | joint count / joint array (stride 0xa0) / joint remap | 0x735c50, 0x737140 |
| +0x494 | `mBlendNum`: active motion layers | getter 0x650dc0 |
| +0x498 | `mMotionCalcFlag` | (the old "+0x49a idle/walk" byte is part of it) |
| +0x4a0 + n*0x140 | `mMotion[8]`, one `uModel::Motion` per layer | array count 8 at 0x73600c; stride 0x140 in 0x73e990 / 0x73ef90 |
| +0xec0 / +0xed0 / +0xee0 | `mNullTransScale` / `mDeltaPos` (root motion of this frame) / `mDeltaQuat` | move subtracts +0xed0 from the position |
| +0xef0 | per-joint blend state (stride 0x40) | 0x73ef90 |
| +0xef4 + bank*4 | motion list per bank (16 banks) | 0x73e650, 0x73ec00 |
| +0xf34 | `KeyFrameNum` | getter 0x7370b0 |
| +0xff8 | `cAnimControl` (motion request front end, see 1.3) | 0x521ac0 `add ecx, 0xff8` |

`uModel::Motion` (base = +0x4a0 for layer 0, +0x5e0 for layer 1), from the reflection and the users:

| rel | abs (layer 0) | field | meaning |
|---|---|---|---|
| +0x04 | +0x4a4 | u16 `mMotionNo` | bank in bits 8..11, index in the low byte; 0xffff = none. Getter 0x41eea0, raw setter 0x73ef50 (number + dirty bit only) |
| +0x06 | +0x4a6 | u16 `mState` | 2 = change pending (applied by 0x73ef90), 0x100 = blending, 1 = end reached, 4 = new this frame |
| +0x0c | +0x4ac | u32 `mAttr` | 0x10 = hold the frame (no advance), 4 = loop, 0x20000 = start relative to the old frame |
| +0x10 | +0x4b0 | f32 | start frame of the request |
| +0x40 | +0x4e0 | f32 `mInterFrame` | blend length in frames |
| +0x44 | +0x4e4 | f32 `mInterCount` | blend countdown |
| +0x48 | +0x4e8 | f32 `mBlend` | 1.0 |
| +0x4c | **+0x4ec** | f32 **current frame** (advanced each update) | `+0x50 = +0x4c; +0x4c += mSpeed * dt` (0x7409b0); the script op at 0x582a20 reads +0x4ec as the frame |
| +0x50 | +0x4f0 | f32 frame at the start of this update | `mFrame` getter 0xa50470 reads it; setter 0x455480 writes +0x4c and +0x50 |
| +0x54 | +0x4f4 | f32 `mFrameMax` | motion length - 1 |
| +0x58 | +0x4f8 | f32 `mLoopFrame` | |
| +0x5c | +0x4fc | f32 `mSpeed` | |
| +0x60 | +0x500 | f32 blend weight | |
| +0x6c | +0x50c | `mInterCurve` | set by 0x4147b0 |
| +0x110 / +0x120 | +0x5b0 / +0x5c0 | `mSequence[4]` / `mPrevSequence` | per-track cursors, built at the start frame by 0x73bbc0 |

**Correction to RE0_NOTES.md [V]:** +0x4e0 is `mInterFrame` (the blend length), not the current frame. That is why the
2026-10-06 probes saw 8.0 idle / 6.0 walking and "a frame that never advances". The running frame is **+0x4ec**.
+0x4e8 is `mBlend`, not the speed; the speed is +0x4fc.

### 1.2 Per-frame advance [V]

`uPlayerBase::move` 0x513190 → `0x50fbf0` (after the think, see 2.1) → vtable slot 31 `0x5286c0` →
`uModel::updateMotion` 0x73e990: applies pending changes (0x73ef90, per-joint blend set-up and `0x73f6e0(layer,
motion data)`), ORs the layer attributes into +0x498, then 0x747260 (pose evaluation) and 0x7409b0 (frame advance per
layer: blend countdown, `frame += mSpeed * dt`, end/loop bits). `0x4146b0` then refreshes `mSpeed` of layers 0 and 1
from the anim control. sGameInfo +0x50 bit 2 makes 0x5286c0 set `mAttr |= 0x10` around the update (script
`PlayerPause2`-style frame hold).

### 1.3 Setting a motion [V]

Three levels, all native:

1. **`uPlayerBase::playMotion` 0x509730** — thiscall(player, u32 motion, u32 flags, f32 interpFrames, f32 startFrame),
   `ret 0x10`, 143 callers (think states, room scripts). It maps the low byte through vtable slot 80 (+0x140) and the
   weapon bank (`0x509cd0`: first loaded bank that has the index), sets layer 0 through 0x521ac0, sets or clears
   layer 1 (low bytes 100..0x69 map to 0x43..0x48), stores the previous motion at +0x68e4, clears the attack-hit
   records +0x699c..+0x69ac, refreshes the hand pose (`0x50f910` → `0x5172d0` → weapon model 0x6510d0) and stores
   `flags & 0x10` in **+0x6a70 = keep root motion** (0 = move cancels the motion's own displacement).
   Argument order checked in the disassembly (0x509850..0x509874): the decompiler's labels are wrong here.
2. **`cAnimControl::set` 0x4144d0** — thiscall(animControl = chara+0xff8, layer, motionNo, flags, interp, start),
   `ret 0x14`. Only four callers: the uCharaBase wrappers 0x521a60, 0x521a90, 0x521ac0 (layer 0), 0x521b00 (any layer).
   It turns flags into attributes (0x414580: 1 → 0x10, 4 → 0x40000, 8 → 0x30000, 2 → no-loop byte) and calls the
   model's vtable +0x6c with `interp * 0.5` and `speed * 2.0`. Speed is `0x4148c0` (vtable slot 51 0x521b90), blend
   curve `0x4147b0` (0x521b40).
3. **`uModel::setMotion` 0x73ec00** (player vtable slot 27 0x653560 → 0x73ec00) — thiscall(model, layer, motionNo,
   interp, start, speed, attr), `ret 0x18`. Resolves the motion in `model+0xef4[bank]` (0x768d30), returns 0 and clears
   the layer to 0xffff when the bank or index is not loaded, clamps the start frame to [0, max], sets frame = start,
   builds the track cursors at the start frame (0x73bbc0) and seeks the key-frame events (0x73b7e0).
   `0x73e650(model, motionNo)` is the engine's own "is this motion available" check.

So **seek is native**: a start frame is a parameter of every level, and the track cursors and key-frame events are
rebuilt at it. Writing `mFrame` through 0x455480 alone does not rebuild the cursors [I: may still sample correctly,
not needed].

### 1.4 State machine the motion is tied to [V]

The brain at player +0x67b8 (`0x533930` returns it) is a two-level state machine:
`think+8` = group, `think+0xc` = state, `think+0x10` = phase. cPlayerThink slots 5 and 6 (0x5077e0, 0x5077c0) call
`table[group][state].{a,b}` from 0xd8a6f0; `changeState(group, state, phase)` 0x4fb2a0 runs exit/enter hooks
(0x4fa7f0, 0x4fa5b0: puts the weapon away, clears flags) only on a change.

| group | table | source file (asserts) | states |
|---|---|---|---|
| 0 | 0xd8ac18 (16) | cPlayerThink.cpp | 0..6 free move (0x506230 + locomotion 0x500e10), 7 (0x506850/0x5021d0, motions 0x16/0x17), 8 (0x500000), 9 (0x506820/0x501180, motions 0xd/0xe), 10 pickup (0x500d00, motion 0x14), 12 (0x502230, partner/sub-think calls), 13 (0x5068f0/0x503100, setHP, bank-5 motions 0x58b..0x594), 15 (0x505f90, motion 0x78) |
| 1 | 0xd8ac98 (9) | cPlayerThinkWeapon.inl | 0, 2..4 aim (0x506c50/0x503c60: 0xa0, 0xab, 0xa5/0xa8/0xab + aim tier), 1 (0x507600/0x5057a0), 5 fire (0x507230/0x504250), 6 (0x507740), 7..8 (0x506f80/0x503e70, weapon-specific motions) |
| 2 | 0xd8ace0 (20) | cPlayerThinkDamage.inl, pl_damage.inl | damage, knock-down, grabbed, death; 12 = fall (0x4fe9e0, motions 199/200, ground search, requestPhase 9) |
| 3 | 0xd8ad80 (5) | | slot 6 only, no motions (0x5036f0, 0x503840 requests a room phase, 0x503a90) |

Several handlers key tables by the motion's low byte (0x4f9700 → tables 0xccb788 / 0xccb7e0) and dereference the
result without a null check. This is the Build 139 crash at 0x4fd370 [N]: a raw motion write while the think ran in
another state. **A puppet whose think never runs cannot hit it**; the danger returns only when the think resumes
(section 3.3).

Partner AI [V]: cPlayerSubThink (vtable 0xcc99b8) slot 5 0x4f7e30 dispatches `SubThinkTable 0xd89988[think+0xd0]`
(at most 0x14 states), slot 16 0x4f44a0 is its sensing. The co-op build already replaces it with a cPlayerThink for
remote-owned characters (`partner_think`) [N].

---

## 2. Splitting the partner's per-frame update

### 2.1 What runs per frame [V]

`sUnit::updateAll` 0x727b50 → group run 0x7279e0 → unit vtable slot 8 **0x526b30** (players): room culling
(sGameInfo +0x50 bit 14 hides units of another room), the CharPause minimal path for non-player ids (bit 0), then
vtable slot 41 = `uPlayerBase::move` 0x513190 (Billy: 0x51b680, which calls 0x513190 then Wesker-outfit extras).

`move` 0x513190, in order:

| step | calls | what |
|---|---|---|
| A. pause branch (sGameInfo +0x50 bit 1) | slot 31, `pos -= mDeltaPos` unless +0x6a70, slot 18 (0x517a60 room light), slot 25(2) (0x76fa90 joint/collision update), 0x71ace0 (world matrix), slot 19 (0x76fa30 collision shapes), +0x6a50 = 1, **return** | the script-paused player: animation only |
| B. pre | slot 47 (0x50a880 light probe +0x6ab0), 0x517c20, 0x50a810, 0x509670, 0x4e4dc0 | |
| C. **brain** | think slot 5 (+0x14), think slot 6 (+0x18) | controller or AI: input, decisions, motion requests |
| D. animation | 0x50fbf0: 0x507800, slot 31 (motion advance), 0x518f70, slot 18, slot 25(2) | |
| E. **locomotion** | root motion into world space (+0x6a60.. anchor), turning, ground 0x526db0, wall collision | physics-driven movement |
| F. commit | 0x71ac90/0x71ace0, slot 19, think slot 16 (+0x40) | |
| G. post | 0x414400, 0x5178b0 (weapon in hand vs holster by frame), area id (0x53faa0 → 0x5295a0), 0x515820 (melee hit records), breath/voice timer +0x6994, 0x55d6a0, 0x4e54b0 | |

### 2.2 The switch points

- **Brain off, animation on: the pause branch A [V].** Room script op 158 `PlayerPause` (0x56c620) sets sGameInfo +0x50
  `|= 2` (and `|= 0x4000`), op 159 `PlayerContinue` (0x56c700) clears both; ops 285/286 (`PlayerPause2/Continue2`,
  0x574ef0/0x574f70) touch bit 1 only. The bit is global (both players), so a puppet sets it **only for the duration
  of the remote character's own move call** and restores it (keeping it if a script had it set). The game_tick move
  scope already brackets exactly that call. Readers of the bit found: move (branch A), 0x526b30 reads bits 0 and 14,
  slot 31 bit 2, and **slot 34 0x512570** (below). None of them runs inside branch A for another unit [V for the
  reads listed; I that no other reader runs inside the bracket].
- **Hits on the character:** vtable slot 34 **0x512570** (thiscall, 4 args, `ret 0x14`; damage table by group/state at
  0xd8d6f8, pl_damage.inl) is the damage/grab entry; it returns 0 while bit 1 is set, the same veto the engine uses
  during scripted pauses [V]. Slot 38 0x512080 (`processHits`) and `setHP` / `onDeath` are already gated for
  remote-owned characters [N].
- **Controller vs AI:** which think object sits at +0x67b8 (cPlayerThink vs cPlayerSubThink, setter 0x50f980) [N].
  In puppet mode the object stays a cPlayerThink (enemy code reads its fields, 3.2) but none of its slots runs.
- **Move/command dispatch:** think slots 5/6 (C) are the only place input becomes motion requests; with branch A
  they are skipped as a block.

What branch A skips and the puppet must cover itself: E (position comes from the network), G's frame-driven weapon
model swap 0x5178b0 and the area id 0x5295a0 (both cheap engine calls on the character, run them after the move [I]),
the light probe slot 47 (run before [I]), the breath timer (cosmetic).

---

## 3. Forcing a motion at a frame

### 3.1 How [V]

Replay the owner's own request through the same function: `cAnimControl::set(chara+0xff8, layer, motionNo, flags,
interp, start)` with the owner's arguments, or `playMotion` 0x509730 for layer 0 with the flags' 0x10 bit cleared
(root motion cancelled). The script op 167 `Player_AnimSet` (0x56cdf0) does exactly the latter on a paused player:
`playMotion(motion, 0x10 | flags, (float)interp, start)`, and polls the end with 0x521a00. Script op 169
`Player_Reset` (0x56d080) calls 0x516440, which returns the character to neutral (`changeState(0,0,0)`, hand pose,
flags) — the clean exit from puppet mode.

Before replaying, check `0x73e650(model, motionNo) != 0`; otherwise setMotion clears the layer and the character
shows the bind pose (no crash).

### 3.2 What else must be set alongside

| state | where | why | how |
|---|---|---|---|
| think group/state/phase | think +8/+0xc/+0x10 | enemy class code reads them to decide attack and grab eligibility: 0x41a22b (group 2, states 6/9), 0x448cc7, 0x4520e0, 0x46c920 (group < 2, not group 0 state 8), 0x4b7442 [V] | raw write of the three ints (data only while the think never runs) [I safe] |
| weapon type | +0x6950 (copy +0xfe8) | selects the motion bank and the held model | `equip_refresh` (existing) [N] |
| hand pose / upper-body mode | +0x68e8 (0x5172d0), +0x6954/+0x6958 (0x517250) | weapon in hand, left-hand item | follows from `playMotion`; 0x5178b0 post-move for frame-driven swaps [V calls, I sufficiency] |
| status flags | +0x68c4 (lock +0x67a0; set 0x512e90, clear 0x512050, read 0x50e1a0, 23 readers outside player code) | read by enemies | mirror after the probe names the bits [I] |
| hit attributes | +0x16ec (0x528ee0 / 0x528ec0; script `PlayerMutekiSet`) | invulnerability windows | not needed while slot 34 is vetoed [I] |
| root-motion flag | +0x6a70 | 0 = position is ours | pass flags without 0x10 [V] |
| HP | +0x1030 | HUD, scripts | existing authoritative `setHp` [N] |
| position, rotation | +0x40, +0x50 | written before the move; branch A builds the matrix and collision from them | [V order] |

Sounds and effects:
- **Motion sounds come for free [V].** `uSoundMotionSe` (DTI 0xdd2c50, vtable 0xcf6c20, update 0x66b610) watches its
  `mpTarModel` and plays the key-frame sounds of each of its 8 layers (reads model +0x4a0 + 0x140*n). Footsteps,
  foley and voice keys follow the replayed motion.
- **Weapon fire is not a motion.** The fire state (group 1 state 5, 0x504250 → per class 0x504320 / 0x504770 /
  0x504e50 / think slot 27 / 0x505440) plays the fire motion, fires through sWeapon (0xdce0a4, vtable +0x60 /
  0x530ec0), starts effects (0x480480, 0x532bb0), animates the weapon unit (its vtable +0x3c with 5 or 0xb) and spends
  ammo from the item block (0x4db9c0) [V calls, I roles]. It needs an event (section 4).

### 3.3 Leaving puppet mode

When the character goes back to input replay or AI (mode change, peer leaves), the think resumes with whatever
motion the puppet shows. Call 0x516440 first (the script's own reset), then hand the think back [V the call, I enough].

---

## 4. Partner actions that are not just motion

| action | engine path | as an event |
|---|---|---|
| walk, run, turn, idle, aim tiers, knife swing | motion requests only (group 0 states 0..6, group 1 aim) | MOTION_EVENT stream; melee hits come from the owner (0x515820 runs only in the normal branch) [V] |
| weapon fire | group 1 state 5 handlers (3.2) | ACTION_EVENT FIRE {weapon type, aim tier, seq}; the receiver plays the weapon unit's fire effect and sound. Damage stays on the owner: `enemy_damage_hook::onPlayerHit` already drops hits whose shooter is remote-owned [V in source]. Finding the smallest effect-only call is a probe item |
| reload | group 1 states 7..8 (0x506f80/0x503e70) | motion stream + INVENTORY (ammo) [N]; ACTION_EVENT RELOAD only if the weapon unit has its own reload animation [I] |
| projectile (fire bottle, grenade rounds) | fire handlers of those classes | ACTION_EVENT PROJECTILE {origin, direction, type}; the owner's hits/explosion through enemy_net |
| equip change | menu close sequence | `equip_refresh` (existing) [N] |
| item pickup | group 0 state 10 (0x500d00) | motion stream; FLOOR_TAKE (existing) [N] |
| item trade, heal from the menu | menu (`inventory_sync` origin byte, `setHP`) | unchanged [N] |
| heal / special in-world use | group 0 state 13 (calls setHP 0x529310) [V call, I meaning] | motion stream; HP from PLAYER_STATE |
| ladder, climb | room script ops 153 `PlayerClimb` 0x56b6e0, 154 `PlayerClimb_Pos`, 176 `ClimbAddChild` [V names] | event_sync replays the thread; the follower must not drive the puppet's motion or position from the script while the stream does: ACTION_EVENT SCRIPTED {thread} start/end, stream wins, position check at the end |
| push block | a group 0 action (state 8 0x500000 or 9 0x501180 — which one is not proven) [I] | motion stream for the body; the block is a unit moved only on the owner: new PROP_MOVE {prop id, final pose} at the end (RE0_EVENT_SYNC.md calls this gap out already) |
| door | sDoorLoad 0x552b50 | `door_sync` / `split_rooms` unchanged [N]; the puppet is moved by `scene::move` like today |
| character switch (V), Solo/Team (E) | think, room phase Change | disabled or adapter-level with a peer (`camera_parity`, `party_mode`) [N] |
| damage reaction | slot 34 0x512570 → group 2 states | owner decides; motion stream + HP; slot 34 vetoed on the puppet |
| grabbed by an enemy | group 2 + the enemy's paired action | owner decides; the follower's enemy takes the owner's grab decision (`enemy_decision`) and the puppet plays the owner's grabbed motion at the owner's position. The enemy may try to start the grab on the puppet (slot 34, vetoed) or snap its position (overwritten next frame). Probe item |
| fall / drop | group 2 state 12 (0x4fe9e0, requestPhase 9 when it is the partner) | motion stream + position; the phase request stays on the owner |
| death | `onDeath` 0x4fcea0 | PLAYER_DIED replays it (existing) [N]; the death motion comes from the stream |
| cutscenes (EventDemo, Movie) | room scripts move both characters | puppet suspended (branch A is already active through PlayerPause); `event_place` unchanged [N] |

---

## 5. Bandwidth and timing

Per owned character, sampled after its own move on the owner:

- **POSE_STATE (unreliable, 30 Hz)**, about 48 bytes: u16 seq, u32 sender ms, character id, room, pos 3×f32,
  quaternion smallest-three (4 bytes), layer 0 {motionNo u16, frame f32, speed f16}, layer 1 {motionNo u16, frame u16
  fixed 1/64}, think group/state/phase (3×u8), weapon type u8, status flags u32. About 1.7 KB/s with framing.
- **MOTION_EVENT (reliable, sequence numbered)**, about 28 bytes: u32 sender ms, layer, motionNo, flags, interp,
  start — the arguments of `cAnimControl::set` as the owner's engine called it (hook on 0x4144d0, filtered to the
  owned character's +0xff8). Typically under 10 per second.
- **ACTION_EVENT (reliable)**: FIRE, PROJECTILE, SCRIPTED begin/end, PROP_MOVE.

For comparison today: PAD_FRAME carries 3 × 208 bytes at 60 Hz (about 37 KB/s) plus PLAYER_STATE 44 bytes at
60 Hz [N sizes from pad_frame.h / state_sync.h].

Receiver: one delayed timeline for both streams (render delay starting at 100 ms, adapted like `JitterTarget`).
Each frame before the puppet's move: position by linear interpolation and rotation by slerp between the two poses
around `now - delay`; every MOTION_EVENT whose time has passed is replayed through `cAnimControl::set` with its own
start frame (the puppet runs the same delay behind, so no frame offset is needed); if the puppet's layer-0 frame
differs from the interpolated owner frame by more than a few frames on the same motion, the same motion is
re-requested at the owner's frame with interp 0 (seek). A missing motion bank (0x73e650 = 0) holds the last good
motion and counts a miss.

Hits keep their path: the owner's own character hits enemies on the owner's machine and `enemy_net` sends
HIT_REQUEST / HIT_APPLIED as today; the puppet never produces damage (the hook drops remote-owned shooters) and never
takes damage (slot 34 veto, processHits / setHP / onDeath gates).

---

## 6. Verdict, risks, probes

**Feasible with caveats.**

Riskiest unknowns, highest first:
1. **Enemy coupling.** Enemies read the puppet's think state and flags, start damage and grabs through slot 34 and
   may position the player in paired grabs. With slot 34 vetoed and the state mirrored this should hold, but the
   enemy's own grab action may wait for the player's side of the pair. Only live.
2. **Scoped pause bit.** Setting sGameInfo +0x50 bit 1 around one character's move: the readers found are inside
   move or in other units' updates; an unseen reader inside slot 18 / 19 / 25 / 31 would see it too. Static check
   done for the obvious ones; only live proves nothing else changes.
3. **Weapon effects.** The fire handler mixes the shot, the effects, the weapon unit animation and ammo; the smallest
   effect-only call is not isolated.
4. **Room scripts on the puppet.** event_sync's follower runs a thread with the subject as the controlled character;
   ops such as `Player_AnimSet`, `ChPosSet`, `PlayerClimb` aimed at the puppet would fight the stream.
5. **Motion banks.** A motion of a weapon whose set is not loaded on the receiver fails (bind pose, no crash). The
   echo test also feeds Rebecca's motion numbers to Billy (different tables, 0x4f9700), so misses there are expected.
6. **Seek quality.** Re-requesting a motion at a frame rebuilds the cursors natively; how visible the seek is while
   blending is a look test.

Minimal live probe (describe only, not run): one PC, `echo_peer.py` extended to echo the two new types, host plays
Rebecca, Billy is the fake peer's character and is the puppet. adapter.ini `partner_mode=state`. Walk, run, aim in
three tiers, fire, reload, knife, pick up an item, take a hit, all in one room with one zombie, two minutes. Read-only
log per frame: Billy's +0x4a4 / +0x4ec against the echoed values, misses from 0x73e650, think +8/+0xc, sGameInfo
+0x50 before and after each puppet move, crashes caught. A second short run with the zombie grabbing the host's
character (Rebecca) checks risk 1 from the follower side only once two PCs are available.

---

## 7. Phased plan (experimental, `partner_mode=input|state`, default `input`)

The shipped path stays untouched while `partner_mode=input`: the new modules register their hooks but do nothing,
and the input-replay modules keep their behaviour. In `state` mode `net_pad` / `input_redirect` do not drive the
puppet and `state_correction` skips it.

| phase | scope | done when |
|---|---|---|
| 0 | this study | report |
| 1 | **Pose core**: config switch, wire structs, owner capture (post-move sample + `cAnimControl::set` hook), receiver puppet (delayed timeline, pre-move pose, scoped pause bit, motion replay with the bank check, think mirror, slot 34 veto), echo test | echo run: Billy repeats Rebecca's movement and aiming 1 s late, no crash, misses logged |
| 2 | **Weapons**: FIRE / PROJECTILE events, effect-only replay found by a probe, reload check, post-move 0x5178b0 / area id / light probe | gunfire and reload look the same on both screens; enemies take damage only once |
| 3 | **Enemy and script coupling**: grabs and damage reactions with the puppet, status flag mirror, event_sync rule for threads whose subject is a puppet (stream wins), PROP_MOVE for push blocks | two-PC session: grabs, knock-downs, ladder and push block in one area |
| 4 | **Transitions**: doors, rooms apart (puppet only in the loaded room), join and resync, cutscenes, mode exit through 0x516440, fallback to input when the peer runs `input` | full two-PC test plan pass in `state` mode |
| 5 | Decide the default | |

### Phase 1 builder brief (Sonnet `builder`, effort high)

> **Task.** Add an experimental partner mode in which the remote-owned character is a pose puppet, behind
> `partner_mode=state` in `coop/adapter.ini` (default `input`). Branch
> `re0-phase4`, adapter `adapters/re0`. Read `CODEMAP.md`, `docs/RE0_NOTES.md` and
> `coop-scratch\re0\POSE_REPLICATION.md` (sections 1-3, 5) first. Do not launch the game; build and
> unit tests only. No commits unless asked.
>
> **Rules.** `partner_mode=input` must behave exactly as now (no new hook changes behaviour unless the mode is
> `state` and the character is remote-owned). Small files by responsibility, named constants for every address and
> offset (in `game.h`), wire structs with `static_assert` sizes, pure logic in a header with a unit test, errors
> handled at the boundary (SEH-guarded game calls like the existing modules), no dead or commented-out code, no
> duplicated helpers. Change the original function instead of layering (e.g. `game_tick`'s single move scope becomes
> a list). Update CODEMAP.md and RE0_NOTES.md in the same change; add the pattern line to docs/re/PATTERNS.md (text
> below).
>
> **Files.**
> 1. `config.cpp/.h`: `partner_mode` = `input` | `state` (unknown value: log and keep `input`).
> 2. `game.h`: constants — `kGameInfoModeFlagsOffset 0x50`, `kPlayerPauseBit 0x2`; `kAnimControlOffset 0xff8`,
>    `kAnimControlSet 0x4144d0` (thiscall(animCtl, layer, motionNo, flags, f32 interp, f32 start), ret 0x14);
>    `kMotionAvailable 0x73e650` (thiscall(model, motionNo) → nonzero); `kPlayerReset 0x516440` (thiscall(player));
>    `kPlayerDamageEntry 0x512570` (vtable slot 34, thiscall, 4 args, ret 0x14, returns 0 = refused);
>    motion layer base 0x4a0, stride 0x140, motionNo +0x4, frame +0x4c, speed +0x5c; think group/state/phase
>    +0x8/+0xc/+0x10; status flags +0x68c4; root-motion byte +0x6a70.
> 3. `game_tick.cpp/.h`: allow several move scopes (vector, registration order; exit in reverse).
> 4. `pose_wire.h` (new): POSE_STATE 0x0140 (unreliable) and MOTION_EVENT 0x0141 (reliable) payloads as in report
>    section 5, encode/decode, `static_assert`s.
> 5. `pose_timeline.h` (new, pure): per-peer buffer of poses and motion events by sender time; `render(now, delay)`
>    returns the interpolated pose (lerp + quaternion slerp, reuse `position_blend.h` helpers, do not duplicate) and
>    the motion events now due, in sequence order, dropping duplicates and stale ones; frame drift decision (seek when
>    over `kSeekFrames`). `tests/pose_timeline_test.cpp`.
> 6. `pose_capture.cpp/.h` (new, owner side, `state` mode only): MinHook on `kAnimControlSet`; when `this - 0xff8` is
>    the locally owned character, call the original, then queue a MOTION_EVENT with the arguments and the send time.
>    Post-move (game_tick post-move list) for the owned character: sample POSE_STATE every second frame (30 Hz).
> 7. `puppet.cpp/.h` (new, receiver, `state` mode, remote-owned character in the loaded room only): move scope entry —
>    write the rendered pose to +0x40/+0x50, replay due motion events through the original `kAnimControlSet` (skip and
>    count a miss when `kMotionAvailable` is 0; clear the 0x10 flag on layer 0 requests so +0x6a70 stays 0), write the
>    think group/state/phase, set the pause bit remembering whether it was already set; scope exit — restore the bit.
>    Patch vtable slot 34 of both player vtables (thunk like `damage_thunk`) to return 0 for a puppet. On leaving
>    `state` mode for a character (peer left, mode off) call `kPlayerReset` once.
> 8. `input_redirect.cpp`, `state_correction.cpp`, `net_pad.cpp`: in `state` mode, do not route the remote pad into a
>    puppet and do not correct its position (one predicate `puppet::active(player)`).
> 9. `state_sync.cpp`: dispatch 0x0140 / 0x0141 to `puppet`; debug_stats counters: poses in, motion events in/out,
>    seeks, misses; F8 lines in `debug_lines.cpp`.
> 10. `tools/echo_peer.py`: echo 0x0140 and 0x0141 too; apply `--offset-x` to the POSE_STATE position.
>
> **Verify.** Build, run the unit tests (pose_timeline plus the existing ones). Do not run the game; report the build
> result, the test output and the exact echo command for the live probe in report section 6.
>
> **PATTERNS.md line to add** (new pattern "A puppet is the engine's own scripted-pause mode, scoped to one
> character" [control, presentation]): Shape: the engine pauses a character's brain for scripted events while its
> animation, root motion cancel and collision shapes keep updating, and scripts play motions on it with a start
> frame. Replicate: enable that mode only around the remote character's own update and replay the owner's motion
> requests through the one function every request uses. Seen in: RE0 sGameInfo +0x50 bit 1 (PlayerPause 0x56c620),
> move branch at 0x5131d4, `cAnimControl::set` 0x4144d0, `Player_AnimSet` 0x56cdf0. Same idea as RE4R's cleared
> MotionFsm2 layers and DD2gether's blocked action requests (docs/FINDINGS.md), and the RE4 Co-op mod's "remote pose"
> avatar timeline (strings in its dinput8.dll: buffered / senderCursor / renderFrame / delayMs, host-traced shots,
> grab tokens, ladder world commits). Trap: RE0 Build 139 wrote the motion number while the think ran (crash
> 0x4fd370); the brain must be off, not just overridden.

---

## Key addresses (all [V] unless marked)

| address | what |
|---|---|
| 0x513190 | `uPlayerBase::move` (slot 41; Billy wrapper 0x51b680); pause branch test at 0x5131d4 |
| 0x526b30 | player unit slot 8 (room cull, CharPause) → slot 41 |
| 0x50fbf0 | animation step inside move (slot 31, slot 18, slot 25) |
| 0x5286c0 / 0x73e990 / 0x7409b0 | slot 31 motion update / uModel::updateMotion / per-layer frame advance |
| 0x509730 | `playMotion(motion, flags, interp, start)` ret 0x10 |
| 0x4144d0 | `cAnimControl::set(layer, motionNo, flags, interp, start)` ret 0x14, the single funnel |
| 0x73ec00 / 0x73e650 | `uModel::setMotion` / motion available |
| 0x455480 / 0x73ef50 | `mFrame` setter / raw `mMotionNo` setter (do not use alone) |
| 0x521a00 / 0x521b90 / 0x4147b0 | motion still playing / speed / blend curve |
| 0x516440 | back to neutral (`Player_Reset`) |
| 0x512570 | slot 34 damage/grab entry, refuses while paused |
| 0x5077e0 / 0x5077c0 / 0xd8a6f0 / 0x4fb2a0 | think slots 5 and 6, state table, changeState |
| 0x4f7e30 / 0xd89988 | partner AI dispatch / SubThinkTable |
| 0x56c620 / 0x56c700 / 0x56cdf0 / 0x56d080 | script ops PlayerPause, PlayerContinue, Player_AnimSet, Player_Reset (opcode table 0xcd57d8) |
| 0x66b610 | uSoundMotionSe update (motion key-frame sounds) |
| player +0x4a4 / +0x4ec / +0x4fc / +0x4e0 | layer 0 motion number / current frame / speed / blend length |
| player +0xed0 / +0x6a70 / +0x67b8 / +0xff8 | root delta / keep-root-motion byte / think / anim control |
