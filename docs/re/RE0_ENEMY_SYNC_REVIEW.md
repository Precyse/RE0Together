# RE0 enemy sync: review and redesign (2026-10-07)

Scope: everything the RE0 adapter does to an enemy, reviewed from the engine up after the Build 158 live session
(host `coop/adapter.log`, guest `coop/peer_76561198150990292.log`, sessions starting 2026-10-07 00:02 and 00:42).
Reported: enemies jitter and teleport on both machines, sometimes stick in an animation, hits register on one
screen only, crits show on one screen only.

## 1. How the game drives an enemy

| Step | Engine | Notes |
|---|---|---|
| Spawn | room load; per enemy slot 36 (0x420ce0 in the base family) reads the spawn record, sets HP, and for some records picks a variant with the global RNG (`rand % 3` into +0x6ab0) | pool sEnemy +0x4b0, 37 slots; same save and room give the same slot order. A killed enemy's flag (0x612 + spawn id) keeps it out of play (flag_sync carries it). |
| Update | vtable slot 41, once per frame from sUnit::updateAll | one function per class (18 implementations) runs AI, action execution, animation and movement together; it ends in `table[state]` on the record +0x67a4. |
| Think | base family (15 vtables): state-2 handler 0x41db20 | a chain of transition checks ending in setAction; picks the target in 0x421b20 (nearer of sPlayer's controlled and partner; the controlled one on a tie). The other 12 implementations decide inside their per-action functions with direct writes of +0x67a4. |
| Action | slot 63 setAction(state, id, a, b) stores the record; the state handlers start the matching motion | 83 direct writes of the record in the non-base classes bypass setAction. |
| Motion | uModel +0x4a0 block, motion number +0x4a4 | owned by the class's state machine; a foreign write crashed the host (Build 139, 0x4fd370). |
| Physics | inside the per-action functions (root motion and turning toward the target) | |
| Damage | slot 35 `damage(attacker, HitPoint*, HitInfo*)` | first calls slot 76 (0x4cbdb0, shared by 37 classes), which rolls the **global xorshift RNG** at 0xe2ccb0 (`randRange(0, 99)` via 0x6600c0) against a per-weapon chance and sets the critical flag +0x6a0c bit 0x1000000; then head/body from the hit point's height, amount from 0x4ca650, `setHP(hp - amount)`, reaction through setAction. Class 0x438b00 rolls the same RNG through 0x660080. |
| Death | HP <= 0 inside the damage function; dead enemies hold -1, rewritten by their update | |

## 2. What Build 158 applies to an enemy

Authority: `control_rule::runsEnemies` (door_travel): alone in a room, you run it; together, the machine that
arrived first ("claim", sent in ROOM_STATE), the host on a tie or while the peer's room is unknown.

| Mechanism | Owner | Non-owner ("puppet") | Writes |
|---|---|---|---|
| ENEMY_STATE 20 Hz | sends pos, quat, HP, record, target | | |
| HP from snapshot | | `setHp(owner hp)` whenever it differs | HP |
| Pose blend | | every tick 50 % toward the owner's position extrapolated from receive-time velocity (capped 0.1 s), rotation nlerp 50 %, snap past 300 | position, rotation |
| setAction refusal | | the enemy's own setAction calls dropped while alive | record |
| Think skip | | 0x41db20 not run (15 vtables) | decisions |
| Record restore | | +0x67a4..+0x67b0 saved before and written back after every slot-41 update | record |
| Record replay | | owner's record applied through the original setAction whenever it differs from the local one, every 100 ms | record, motion start |
| Target override | | after both selectors, the owner's target written into +0x6b80/84/88 | target fields |
| Hit routing | local shooter: apply, send HIT_APPLIED | local shooter: HIT_REQUEST, no local apply | |
| Hit receive | accepts HIT_REQUEST only (drops HIT_APPLIED) | accepts HIT_APPLIED only (drops HIT_REQUEST); runs the damage function with its own RNG | HP, reaction |
| Room entry | first to arrive owns; nothing waits for the other machine | | |

### Where two writers meet

1. **Position: the enemy's own movement vs the 50 % per-tick blend.** The update still moves the enemy (root motion,
   turning), and the blend pulls it every tick toward a target extrapolated from receive times (network jitter
   becomes velocity noise). Whenever the local action differs from the owner's, the two pull in different
   directions: visible jitter, and a snap past 300.
2. **Record: four writers.** The AI (refused), the direct writes (undone by the restore), the replay (every 100 ms
   while different) and the damage function's reaction (refused). An action that ends through setAction(2, ...)
   is refused, and an action whose own code moves on is put back by the restore, so a finished action holds its
   last frame until the owner's record changes; when the owner repeats the same action the record never differs
   and the puppet stays stuck. When the sub-words a/b move during an action, the replay re-applies every 100 ms
   and restarts the motion: the enemy loops the first frames of an animation.
3. **HP: damage function vs snapshot.** A replayed hit lowers local HP, then a snapshot sent before the hit
   puts the old value back. Guest log 00:49:10-00:49:17: `slot 0 hp 95 -> 85`, `hp 69 -> 85` (a stale
   snapshot undoing a replayed 16-point hit), `hp -15 -> -1`. Setting a positive HP on a locally dying enemy can
   also leave it in a dying motion with HP > 0.
4. **Crit roll on both machines.** Every machine that runs the damage function rolls its own RNG. Guest log
   00:48:04 `slot 1 hp -68 -> -1` and 00:48:21 `slot 0 hp -57 -> -1`: the guest's replay of the host's hit
   dealt a critical's damage the host never dealt. This is the "crit on one screen" that 6edd8fb did not fix.
5. **Authority hand-over at room entry.** Load times differ by seconds (00:25 session: guest arrived 3 s before
   the host). The first to arrive claims the room and runs its enemies alone until the other arrives; the
   late machine's freshly spawned enemies are then blended or snapped to positions seconds ahead (teleport on
   entry). While the peer's ROOM_STATE is in flight the two machines can disagree (guest log 00:25:28-31:
   `run here`, `run on the peer`, `run here` within 90 ms).
6. **Hits during a disagreement.** HIT_REQUEST is only accepted by a machine that believes it is the owner, and
   HIT_APPLIED only by one that believes it is not. When both believe the same thing, every hit is dropped on
   the other side: shots that land on one screen only.
7. **Owner/puppet switch.** Leaving a shared room makes the puppet an owner in one frame: the refusal, think skip
   and restore end at once with a record the AI did not choose.

### Symptom to cause

| Symptom | Cause (above) | Evidence |
|---|---|---|
| Jitter on both machines | 1, 2 (pose blend fighting the enemy's own movement while its record differs) | code: enemy_state `followPose`, enemy_action `onUpdate`; enemy logging too thin to measure (no per-slot drift lines) |
| Teleport | 1 (snap past 300), 5 (late machine aligned to an enemy seconds ahead) | 00:25 session arrival times; the room entry has no barrier |
| Stuck in an animation | 2 (refused end-of-action, restore, 100 ms re-apply loop) | code paths; Build 139 history (frozen enemies when the update was skipped) |
| Missed hits on one side | 6, plus the old enemy_net drain fault (00:13:12 guest, pre-6edd8fb) | logs |
| Crit on one screen | 4 | 00:48:04, 00:48:21, 00:49:17 guest lines |
| "Two systems fighting" | 1, 2, 3 | — |

## 3. Approaches

**(a) Single-writer puppet.** The non-owner runs no AI; one channel applies the owner's state. In RE0 the AI,
action execution, motion and physics are one slot-41 function per class (18 implementations), and the motion
number belongs to the class's state machine (a foreign write crashed the host). Skipping the update froze
enemies (Build 139); skipping only the AI needs class-by-class surgery that 12 implementations do not allow
(their decisions live inside the action code). Build 158 is this design half-done, and the half is what fights.
Rejected: RE0 gives no clean seam between "decide" and "animate".

**(b) Both simulate, decisions synced.** Each machine runs the enemy natively (it can never stick in a state the
engine would not reach), and only the owner's choices and outcomes are shared. RE0 enemies are slow and
room-bound, and a room starts every enemy from its spawn record, so two machines that start the room at the same
moment, with the same target and the same decisions, stay close. Cost: an RNG-driven choice can differ until the
owner's next decision arrives; positions drift and need a correction.

**(c) Hybrid (chosen).** (b) plus a single owner for outcomes and a gentle position correction:

- **Room-entry barrier** (the user's idea, adapted to the engine): ROOM_STATE carries the room the sender's door
  leads to. A machine that loads a room the peer is still travelling to holds its whole world (the proven
  menu-freeze of sUnit::updateAll, "Waiting for partner") until the peer reports that room, the peer stops
  travelling there, or 15 s pass. Both rooms then start from their spawn records within half a round trip.
  The engine loads the room after the door animation (the door phase goes idle first), so the hold sits exactly
  between "both doors done" and "room runs".
- **Native enemies on both machines.** Removed: setAction refusal, think skip, record restore, slot-63/41
  thunks, the 50 % pose blend with extrapolation, the 100 ms record re-apply.
- **Decisions:** the owner's record travels in the snapshot; a non-owner gets it through the class's own
  setAction once per owner change, and only when its own enemy has not reached the same action id within
  100 ms. Never repeated for an unchanged owner record.
- **Target:** the owner's target replaces the local selector's result (the only writer after the selector; the
  same player is chased on both screens). Kept.
- **Position:** when a snapshot arrives the error to the owner's position is measured once and removed over the
  next ticks (20 % per tick, on top of the enemy's own movement); under 12 units nothing, over 300 a snap.
  Rotation is left to the enemy's own turning except on a snap.
- **Hits and deaths: the owner of the room's enemies decides, everyone replays exactly.** A hit landing on the
  owner's screen runs there; one landing on the other screen goes to the owner as HIT_REQUEST. The owner records
  the enemy's HP and the RNG state, runs the damage function, and sends HIT_APPLIED with both. Every other
  machine sets that HP, swaps the RNG state in, runs the same damage function (same crit, damage, reaction and
  death), and restores its RNG. The hit point travels relative to the enemy, so the head/body test gives the
  same answer on both screens. Whoever receives a request applies it and whoever receives an applied hit replays
  it, so a momentary disagreement about the owner no longer loses hits.
- **HP from the snapshot** is only a backstop (damage the owner took from something that is not a player): it
  waits 500 ms after a replayed hit, and a lethal value only applies after the owner has reported the enemy dead
  for 1 s.

Why (c): it removes every second writer instead of arbitrating between them, leaves the engine's state machines
whole (no stuck states, no crash risk from foreign motion writes), makes hits exact rather than approximate, and
attacks the largest divergence source (start-of-room timing) at its root.

## 4. Live checks (next session)

- Room entry together: `room_gate: holding scene 0x.. for the peer` then `room_gate: released scene 0x.. (peer
  arrived|peer not coming|timeout) after N ms`; the second machine logs no hold. Timeouts mean a stalled peer.
- Hits: host `enemy_net: hit slot N applied hp A -> B rng xxxxxxxx` and guest `enemy_net: hit slot N replayed hp A
  -> B` with the same A and B. `enemy_net: hit dropped (<reason>)` names every lost hit.
- Position: `enemy_state: slot N corrected drift D` (1/s per slot) and `snapped slot N drift D`; a steady stream
  over 100 means the decisions diverge.
- Decisions: `enemy_state: slot N cued action (s,id,a,b), local (s,id,a,b)`.
- Crit parity: a guest line `hp X -> -1` with X far below 0 should no longer appear.
