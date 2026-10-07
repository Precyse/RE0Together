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

The bar (from the user): players playing the vanilla game with online functionality; every piece of enemy state has
one writer; a correction goes through the game's own function for that state; nothing visible at normal latency.

**(a) Single-writer puppet.** The non-owner runs no AI and one channel applies the owner's state. In RE0 the AI,
action execution, motion and physics are one slot-41 function per class (18 implementations), and the motion number
belongs to the class's state machine (a foreign write crashed the host). Skipping the update froze enemies (Build
139). Build 158 was this design half-done, and the half is what fought. Rejected.

**(b) Both simulate natively, only outcomes shared.** Vanilla behaviour on each screen, never stuck. But the AI's
random choices differ, so the two screens disagree about what an enemy does, and an attack that bites one player on
one screen misses on the other.

**(c) Both simulate, the owner's decisions cued and positions corrected** (the first version of this branch). Each
correction was a second writer: a per-tick position write on top of the enemy's own movement, and a setAction cue
racing the local AI's own choice. Rejected for the same reason as Build 158, only milder.

**(d) Chosen: one decision maker, one outcome maker, native execution.** The engine itself separates "decide" from
"execute" in the base family, and that seam is where the owner's decisions enter:

- **Decisions (base family, 15 vtables: uEnemy10..1a, 50, 51, 5a, 5b, 6a, 6b).** These classes decide only in their
  think step 0x41db20 (state 2): transition checks and a per-action think call that end in setAction 0x4cc670 (a plain
  store of the record; their code has no direct writes of it). The owner's think step runs untouched and every record
  it chooses goes to the peer with the pose it chose it from (ENEMY_DECISION, reliable). On the follower the think
  step also runs (its flags, timers and per-action call), the setAction calls it makes are dropped, and the owner's
  newest decision is stored through the real setAction at that same point. Execution, movement, animation and hit
  reactions stay native. If the pose there is more than 40 units off, it is set to the owner's decision pose before
  the action starts (between two actions; normal play stays under it).
- **The other 23 vtables** decide inside their action code (no seam). They run natively on both machines; their
  outcomes (hits, deaths) are exact and their start is shared (barrier). Their decisions are not synced; finding a
  seam per class is open work.
- **Hits and deaths: the room's enemy owner decides, everyone replays exactly.** A hit landing on the owner's screen
  runs there; one landing on the other screen goes to the owner as HIT_REQUEST. The owner records the enemy's HP and
  the random state, runs the damage function, and sends HIT_APPLIED with both and the HP after it. The other machine
  sets that HP, swaps the random state in, runs the same damage function (same crit, damage, reaction and death),
  restores its random state, and checks the result against the owner's HP after. Damage no player dealt (each
  machine sees its own copy) is applied by the owner only, and its HP after travels. Whoever receives a request
  applies it and whoever receives an applied hit replays it.
- **Target.** The base classes' selector 0x421b20 and uEnemy2b's 0x439e90 are hooked; on the follower the hooked
  selector's result is the owner's character, so the selector call stays the only writer of the target fields.
- **Door barrier** (the user's idea). When one player takes a door in TEAM, both machines play that door and load
  the room inside it; the room starts when the door finishes (room phase DoorLoad -> Main). The door's own finish
  check 0x551c70 is hooked: the faster machine reports its door ready and its door waits on its last frame until the
  peer's same door is ready, so both rooms start their enemies from the spawn records together. Nothing visible is
  frozen; "Waiting for partner" shows only past 2 s; released after 5 s at most, or when the peer turns away or the
  link drops. A door one player takes alone, or a partner heading into the same room on its own, never waits.
- **Following mid-room** (walking into a room the peer already runs, or after a barrier timeout). On the first
  snapshot each enemy takes the owner's pose (if more than 40 off) and HP once, and its think step the owner's current
  record until the first decision arrives.

## 4. Who writes what

Follower = the machine sharing the room with the peer that runs it (`enemy_state::followsOwner`: following, and an
owner snapshot within the last 15 of its own ticks, 500 ms at 30 fps; a world held on both sides is not silence). Owner = everything vanilla, plus reporting.

| Field | Owner | Follower | Why nothing else writes it |
|---|---|---|---|
| Record +0x67a4..+0x67b0, base family | its own setAction calls | setAction calls outside think (executor, damage reaction, scripts) as in vanilla; inside think only the owner's decision, through the real setAction | the follower's think-step setAction calls are dropped; the base family has no direct record writes |
| Record, other 23 vtables | own code | own code | nothing of ours writes it |
| Motion, animation, physics | own update | own update | nothing of ours writes them |
| Position, rotation | own movement | own movement; at a decision more than 40 off, the owner's decision pose (before the action starts); once at follow start | the per-tick blend and correction are deleted |
| HP | its damage function | the replayed damage function (from the owner's HP and random state); the owner's HP after when a replay or a non-player hit ends elsewhere; once at follow start | the follower never runs its own damage function on a shared enemy (hits go to the owner) |
| Death | HP in its damage function, then think | the same, replayed | no periodic HP writes |
| Target +0x6b80/84/88 | the selector | the hooked selector, with the owner's character | one call writes it |
| Global random state 0xe2ccb0 | the game | swapped in for one replayed damage call and put back right after, on the game thread | |

The owner's hits and decisions reach the follower through one ordered queue (enemy_net), so a decision the owner
made before a hit is never applied after that hit's reaction (the replay drops it).

## 5. Conditions

| Condition | Handling | Test |
|---|---|---|
| Room entry together, either first | the faster machine's door waits at its finish until the peer's is ready; it then owns the room (first in) | room_gate_rule_test |
| Room entry together, simultaneous | each reports ready before it checks, so neither waits for long | room_gate_rule_test |
| Doors taken alone, split mode, partner heading the same way | never waits: only a door both machines play holds | room_gate_rule_test |
| Follower thinks before the owner's first decision | its think waits (applies nothing) up to 15 ticks from following start, then decides itself | enemy_follow_rule_test |
| Load gap 0-5 s | barrier, invisible under 2 s; past 5 s the first plays on and the late one aligns on its first snapshot | room_gate_rule_test |
| Peer disconnects mid-room | no snapshots: after 15 ticks (500 ms) the follower's think decides again; the owner keeps running | enemy_follow_rule_test |
| Door taken while enemies act | a room change resets the follow state and waiting decisions; events of the old room are dropped by their room byte | enemy_follow_rule_test, code |
| Both kill the same enemy at once | the owner applies both in order; the second lands on a dead enemy as in vanilla; replays follow the same order | code |
| Hit after the enemy died here | replay dropped and logged; the owner's outcome already stands | code |
| Hit after a room change | dropped by its room byte, logged | code |
| Packet loss, reordering, late snapshot | events are reliable and ordered; a snapshot or decision with an older or repeated sequence number is dropped | enemy_follow_rule_test |
| Owner leaves the room | the remaining machine is owner: its think decides from the next frame, waiting decisions dropped; nothing was switched off, so nothing switches back on | code |
| Both believe they own (or neither does) | hits are applied or replayed whoever receives them; no one follows a silent owner, so no enemy waits for a decision | enemy_follow_rule_test |
| Resync mid-combat | the join snapshot does not touch enemies; the follow state carries on | code |
| Split party | each machine owns its room; hits and decisions of another room are dropped | code |
| Cutscene or scripted enemy | script setAction calls run natively on both (only think is substituted); a cutscene or menu on either side holds the other's world | code |
| Game over and continue, save load | a room load: follow state and waiting decisions reset | code |

## 6. Live checks (next session)

- Door together: the faster machine logs `room_gate: door into scene 0x.. waits for the peer's` then `room_gate: door
  into scene 0x.. released (peer ready|peer not coming|timeout) after N ms`; the slower one logs no wait. Timeouts
  mean a stalled peer. Note what the screen shows during a wait.
- Hits: host `enemy_net: hit slot N applied hp A -> B rng xxxxxxxx` and guest `enemy_net: hit slot N replayed hp A ->
  B (was X here)` with the same A and B; `replay diverged` should never appear. `enemy_net: hit dropped (<reason>)`
  names every lost hit; `(not a player)` marks damage from explosions or the like.
- Decisions: `enemy_think: slot N took the owner's decision (...)` on the follower (1/s) and the F8 line `enemy
  decisions sent/applied`; `enemy_think: slot N realigned at a decision, drift D` should be rare.
- Drift of the other 23 vtables: `enemy_state: slot N drift D (record s,id)` (1/s, only above 40).
- Follow start: `enemy_state: slot N aligned with the owner (drift D, hp A -> B)`.
- Crit parity: a guest line `hp X -> -1` with X far below 0 should no longer appear.
