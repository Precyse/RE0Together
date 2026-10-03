# How they built it: mechanism-level deep dive

Companion to `MULTIPLAYER_MODS.md` (overview, comparison, ranked adoption list). This file is code-level: data structures, call flow, rates, edge cases, then DS2's current approach beside it, the gaps, and concrete changes. Written 2026-10-03 from shallow clones of the public repos. Everything here is paraphrased ideas; Nitrox, TiltedEvolution, SkyMP and CyberpunkMP are GPL/AGPL or similarly restricted, so none of their code may be copied into this repo.

Sources read (paths relative to each repo):
- **Tilted** = https://github.com/tiltedphoques/TiltedEvolution (Skyrim Together Reborn / Fallout Together): `Code/client/Services/Generic/{CharacterService,InventoryService,ActorValueService,QuestService}.cpp`, `Code/client/Systems/{AnimationSystem,InterpolationSystem}.cpp`, `Code/client/Games/Skyrim/TESObjectREFR.cpp`, `Code/encoding/Structs/{AnimationVariables,ActionEvent,AnimationGraphDescriptor}`, `Code/server/Services/*`, `Code/server/Components/OwnerComponent.h`.
- **Nitrox** = https://github.com/SubnauticaNitrox/Nitrox: `Nitrox.Server.Subnautica/Models/GameLogic/{SimulationOwnership,StoryManager}.cs`, `.../Entities/{EntitySimulation,SimulationWhitelist}.cs`, `NitroxClient/GameLogic/{RemotePlayer,SimulationOwnership,CorrectedTimeManager,MovementHelper}.cs`, `NitroxClient/MonoBehaviours/{MovementBroadcaster,MovementReplicator,MultiplayerVehicleControl,PlayerMovementBroadcaster}.cs`, `.../Vehicles/WatchedEntry.cs`, `NitroxClient/GameLogic/InitialSync/*`.
- **SkyMP** = https://github.com/skyrim-multiplayer/skymp: `skymp5-server/cpp/server_guest_lib/{ActionListener,MpObjectReference}.cpp`, `docs/docs_onhit_and_damage.md`.
- **CyberpunkMP** = https://github.com/tiltedphoques/CyberpunkMP: `code/client/App/World/VehicleSystem.cpp`, `code/assets/redscript/World/{NetworkWorldSystem,VehicleSystem}.reds`, `code/protocol/*.proto`.
- **BeamMP server** = https://github.com/BeamMP/BeamMP-Server: `src/TServer.cpp`. FiveM's server is closed source, so FiveM appears only through docs (`https://docs.fivem.net/docs/scripting-manual/networking/onesync/` was unreachable; the OneSync reference at https://docs-backend.fivem.net/docs/scripting-reference/onesync/ and forum threads are the basis).

DS2 sources read: `CODEMAP.md` (adapters/ds2 rows), `docs/DS2_NOTES.md`, `adapters/ds2/src/{anim_sync,equip_sync,vehicle_sync,fact_sync,player_sync}.cpp`, `adapters/ds2/src/ds2/{remote_player,remote_animation,remote_ride,equip_probe}.cpp`.

---

## Rates and timing at a glance

| System | Send rate | Render delay / interpolation | Reliability |
|---|---|---|---|
| Tilted movement + animation variables | every 100 ms (10 Hz), one batched message for all local actors | render at "now minus 300 ms", two-sample linear interpolation on a shared server-clock tick | unreliable snapshot; discrete action events ride in the same batch |
| Tilted health | additive deltas, immediately; deltas under 1 HP batched and flushed each 1 s; death state polled each 250 ms | none | reliable |
| Tilted weapon-draw state | polled each 500 ms, sent on change | applied after 0.5 s then re-applied after 2 s on the remote | reliable |
| Nitrox vehicle movement | 30 Hz, only if moved more than 0.05 units or rotated more than 0.05 degrees, plus a 5 s heartbeat and a 0.2 s safety window | `4 x 33 ms` plus an adaptive "max allowed latency" that grows at once on a spike and shrinks slowly; ring buffer of snapshots, expiry `5 x` interpolation time | position stream unreliable, ownership changes reliable |
| Nitrox player movement | per-frame broadcaster, sends position, velocity, body and aim rotation | velocity set from corrected position error each physics step; snap if more than 20 m off | |
| BeamMP | position packets over UDP to everyone except the sender, reliable TCP for spawn, edit, reset, paint | client side | server drops a position packet whose player id is not the sender |
| DS2 today | PLAYER_STATE 60 Hz, VEHICLE_STATE 30 Hz, ANIM_STATE 30 Hz changes plus a snapshot each 1 s, EQUIP_STATE on change (checked each 500 ms) | extrapolation from last report; no shared render delay | position and animation unreliable; cargo, equip, load, facts reliable |

---

## 1. Remote player spawn and lifecycle

### How they do it
**Tilted (actors in general, players included).**
- Each client that loads an actor announces it to the server with a cookie and gets back an assignment: server id, an `Owner` flag, an ownership epoch, current actor values, inventory, dead flag, weapon-drawn flag and an `ActionsToReplay` chain (the last few animation actions so a late viewer starts in the right pose; the chain can ask the receiver to reset the graph first). If the client is not the owner it becomes a *remote* of that server id.
- A remote starts life as **pure data**: an entity holding the server id, an interpolation buffer and an animation queue, flagged "waiting for 3D" with the full spawn request kept in it. No game actor exists yet. A per-frame spawn pass checks whether the entity's interpolated position falls inside the grid cells the local player has loaded; only then does it create (or look up) the actor, `MoveTo` the cell, apply actor values, kill or respawn to match the dead flag, set the remote flag, and start replaying actions. So **the body appears exactly when its surroundings can exist**.
- Messages with an epoch of 0 are ignored, a second spawn for a server id that already exists is ignored, and a spawn for a form that is mid-assignment is ignored. Temporary actors are created from a serialized appearance buffer; persistent ones (static refs) are looked up and merely flagged remote.
- Despawn has two shapes: for temporary actors the actor is deleted; for persistent world actors the remote flag is cleared and the actor goes back to the game's own AI. If the actor unloads locally the pending assignment is cancelled so a late answer destroys the stale entity instead of attaching to nothing (and, if the answer grants ownership, ownership is declined back).
- "Decline" is a real message: an owner that cannot take the actor (no 3D yet, unknown form) says so, and the server moves on to the next candidate (see section 4).

**Nitrox (players).** A remote player is a body object cloned from the game's player model, given a rigidbody with interpolation and its own animation controller component (section 2). Joining runs a **dependency-ordered list of initial-sync processors** (clock sync, local player, remote players, global root entities, story goals, PDA, equipped items, quick slots, simulation ownership, player position, preferences) behind a wait screen; each processor declares which others it needs. `ResetStates` clears seat, chair, sub root and arm IK when a remote leaves or changes mode.

**CyberpunkMP.** The server announces an entity; the client asks the game's dynamic-entity system for an NPC record built for puppets, flagged always-spawned and non-persistent with a mod tag, and a `SpawningComponent` stays on the entity until the game's own entity-attached callback fires. Position updates before that are held, not applied. Server-side movement packets drive a flecs system that interpolates.

**cyber.rest.** Spawn manager that waits for terrain under the puppet before showing it (ground snap).

**Kenshi (low confidence docs).** Remote bodies are created by replaying the game's own character factory call; on disconnect they are teleported far underground before the registry is cleared, because clearing the registry first loses the pointers.

### DS2 today
`ds2/remote_player.cpp` creates a real second player entity (net player, added to the player manager, spawned from Sam's resource) once Sam's state machine has run 8 s (the "gameplay gate"), after a peer reports. It is forgotten when the gameplay gate closes or Sam's entity changes (title screen, reload). Each frame it is placed at the peer's pose or rides the peer's vehicle. The first placement is 2.5 m ahead of Sam.

### Gaps
1. No "materialize only when the place exists" state. The body is placed at the peer's pose every frame. I found no check for whether the area around the peer is streamed in on this machine, so a far-away peer can put a physics body into unloaded terrain (the failure cyber.rest and Tilted both guard against).
2. The spawn is gated on Sam's gameplay clock and on a peer report, but the peer's liveness is only a 3 s stale timer in `player_sync`. A short report gap and a real despawn look the same.
3. No spawn-time state catch-up: when the body appears it starts in default animation and without the partner's current equipment or vehicle, then converges over the next reports. Tilted ships the replay chain with the spawn; Nitrox runs a full ordered initial sync.
4. Nothing makes a late or rejoining guest receive world state in a defined order.

### Concrete changes
- Add a three-state body: **Tracked** (data only: last pose, velocity, vehicle id, anim snapshot, equipment), **Parked** (entity exists but hidden and not simulated) and **Placed**. Move to Placed only when a ground probe at the peer's position hits loaded collision (or the peer is within a conservative radius of Sam's own streamed area). Park again on a streaming transition. Implementation sits next to `remote_player::setTarget`.
- On Tracked to Placed, apply in this order: equipment, animation snapshot (the last full ANIM_STATE snapshot), vehicle ride request, then pose. Keep the last full anim snapshot per peer to make this possible (the receive side already stores peer changes by slot).
- Split "peer silent" from "peer gone": treat only a launcher PEER_DOWN (or a much longer silence) as gone; short silence freezes the body.
- Write the join order as an explicit dependency list like Nitrox's processors (clock offset, facts snapshot, cargo snapshot, vehicle states, then bodies) and log each step; this also gives the rejoin path a checklist.

---

## 2. Animation sync

### How they do it
**Tilted: behaviour graph variables per actor.**
- Variables are read from the actor's active behaviour graph's variable set and packed by a per-graph **descriptor**: three index lookup tables (booleans, floats, integers) naming which graph variable indices are synced, with compile-time caps (at most 64 booleans and 63 floats plus integers per descriptor). Descriptors are registered per skeleton hash; if a mod has an unknown graph, a "behaviour var patch" tries to build one, and if none exists nothing is synced for that actor.
- The player always uses its third-person graph (index 0) for sending, regardless of the camera mode. This avoids the first person graph's variables.
- A sample is a flat triple of vectors (booleans, integers, floats) in descriptor order. The wire format for a diff sends three var-int counts, then a bitset made of all boolean values followed by one "changed" bit per integer and per float, then only the changed integers and floats. Booleans are always sent (bit-packed) because they are cheap.
- Samples are bundled with position, rotation, direction (the AI "movement direction" value that locomotion graphs read), cell and world space, into one message every 100 ms for all actors the client owns, stamped with a synchronised server-clock tick.
- Receive: samples go into a time-ordered buffer. Each frame the client renders at `now - 300 ms`; it keeps at most the two samples around that time, interpolates position and rotation linearly (yaw is wrapped with shortest-angle delta, pitch clamped), and **loads the later sample's variables wholesale into the graph** each frame, and sets the movement direction. The variables are not interpolated (the graph's own blends smooth them).
- **Discrete things go separately as action events.** A hook on the game's action dispatcher records `(tick, actor, action, target, idle, state flags 1 and 2, event name, target event name, full variable snapshot)`. They travel as differentials against the previous event (field-level), land in a per-actor queue, and are replayed when their tick comes: set the actor's state flags, load the variables, then force the action through the game's own action mediator. Events can arrive before the actor exists; an "early animation buffer" stores them until the assignment answer says whether they are local (then they are sent) or remote (then dropped).
- Replay safety: don't play while the graph isn't ready (leave the event queued); optionally revert the graph manager first when the chain says so; keep the last played action so a loaded actor can repeat it.

**Nitrox: named animator parameters.** The remote body's animation controller exposes named parameters (underwater, in seamoth, in exosuit, piloting chair) and takes velocity; while seated the controller's automatic update is turned off and the body is attached to the vehicle's player position node with hand IK targets. Locomotion is driven by velocity computed from the corrected position delta each physics step.

**cyber.rest.** Move commands sent to the puppet so its stock locomotion animates.

### DS2 today
`anim_sync.cpp` + `ds2/remote_animation.cpp`: sample every graph-bound variable of Sam's animation manager on Sam's own `MsgGetAnimatedPose` at 30 Hz, send changed ones as `{u16 index, u8 type, value}` entries (unreliable), full snapshot each 1 s. Receiver writes the peer's variables into the remote's graph just before its pose is evaluated (the hook runs on the remote's `MsgGetAnimatedPose`). Peer data is dropped after 3 s of silence.

### Comparison
This is already stronger than Tilted in coverage: every graph-bound variable instead of a hand-written descriptor, delta by index, a periodic snapshot to heal loss, and application at the exact graph-evaluate point. Weaknesses against Tilted and Nitrox:
1. **No timestamps and no render delay.** The variables are applied as they arrive while the position comes from extrapolation. Tilted stamps both with one clock and renders both from the same delayed buffer, so a pose and the motion it belongs to line up.
2. **One-shot edges can be lost.** A boolean or integer that flips for one or two evaluations (a stumble trigger, a throw, a pickup) is sent once, unreliably. The 1 s snapshot cannot recover something that already went back. Tilted's separate action events and DD2gether's 5x edge re-send exist for this reason.
3. **No catch-up on spawn** (section 1).
4. **No filter for variables that must not be copied** (camera-mode, locally derived, UI related). The "graph-bound" test is the only filter. Tilted keeps an explicit synced-index list and forces the third-person graph for the same reason.
5. No bandwidth classes: all variables go at the same rate (the code logs bytes/s every 5 s, which helps).

### Concrete changes
- Add a **timestamp** (sender's synchronised clock, see section 8) to ANIM_STATE and PLAYER_STATE and render both from a small delay line (start with 100 to 150 ms, because 60 Hz and 30 Hz sources are denser than Tilted's 10 Hz), using Nitrox's adaptive delay (grow instantly on a latency spike, shrink slowly) so the delay is as low as the link allows.
- Classify variables once at startup by watching which ones toggle for fewer than N evaluations ("pulse-like") and send those entries **reliably, or repeat them 3 to 5 times in successive reports**. Cheapest version: any bool that goes true then false within 0.5 s is resent in the next 3 reports.
- Keep the last full snapshot per peer so a (re)spawned body gets it before its first evaluation.
- Add a per-variable denylist (log which variables change only locally, such as camera or UI) learned from the `bytes/s` log; start with an empty list so nothing is hidden by default.

---

## 3. Vehicle enter, exit and seats

### How they do it
**CyberpunkMP (the clearest flow).**
1. The local player enters a vehicle. The client hook asks "is this vehicle one the server already knows?" If yes it sends the server vehicle id and the seat name hash; if not it sends the vehicle's database record id, position, heading and seat, and remembers the *local* entity id as the vehicle it controls.
2. The server creates (or finds) the vehicle entity, tells the entering player it now **controls** that vehicle id (a separate "control assigned" message), tells everyone to spawn the vehicle (record id, position, rotation) if they don't have it, and broadcasts "character X entered vehicle Y, seat S".
3. On a remote client: if the vehicle's entity already exists, mount at once; otherwise **queue the mount keyed by the vehicle's entity id** and wait until the engine's entity-attached callback for that entity reports ready, then flush every queued mount for it. Exit is a separate message that calls the game's exit function and removes an "attached" tag.
4. The mount is a game event queued on the character with a slot name, an instant flag and a "skip high-level state" flag. The author also tried a walk-to-the-door variant (kept in comments) and the direct component call; the instant event is the one shipped.
5. For a vehicle driven by someone else, the vehicle is switched out of "player controlled", the engine is started, two flag bits are set and the physics body is made **kinematic** so network position, not local physics, moves it. The local driver's own vehicle skips this block (that is the "local game id equals vehicle game id" test).

**Nitrox.** Driver takes an **exclusive** simulation lock on the vehicle when entering and releases or downgrades to transient on exit. A movement watcher on every vehicle the client simulates sends position and rotation at 30 Hz when changed (extra steering, throttle and arm targets when it is the driven vehicle). Remote vehicles use a replicator with a snapshot ring buffer and the adaptive delay above; velocity is set to close the position error each physics step, with a hard snap if more than 20 m away. The remote player body is made kinematic, attached to the vehicle's player position, with arm IK targets and animator flags. A defensive rule: the replicator component is created on demand when a remote enters a vehicle that was docked during the join, because replicators otherwise only appear after ownership packets.

**BeamMP.** Vehicle id is a pair (player id, vehicle id). Position packets carry that pair; the server checks that the pair's player id is the sender before relaying (so a client cannot move someone else's vehicle), forwards unreliably to everyone but the sender, and stores the latest. Spawn, edit, reset and paint are reliable and pass through server-side veto events (`onVehicleSpawn` can refuse; the server also enforces a per-player car limit).

**FiveM/RedM (docs only).** Vehicle entities have a network owner chosen by proximity; the occupant of the driver seat effectively simulates it; ownership migrates and must never be cached.

### DS2 today
Driver owns the vehicle; VEHICLE_STATE 30 Hz (seq, role, id, position and rotation rows), extrapolated, left where reports stop after 500 ms; the second occupant gets role passenger; load by VEHICLE_LOAD reliable. The remote body rides through the game's own ride request (kind 3 and target id on the owner, plugin phases on-foot, ride-on, drive, ride-off), is placed at the driver door, `ClearParent` when ride-off ends; `setdriver_guard` stops `SetDriver` for the remote from changing the local manager's driven ids.

### Gaps
1. **No readiness gate on the vehicle copy.** CyberpunkMP's queued mounts exist because the vehicle entity may not be loaded when the mount message arrives. DS2's ride request appears to be issued from `remote_ride::tick` whenever the peer is driving; if the local copy of the vehicle is not loaded or still streaming it fails (`Driven.failed` is logged once).
2. **No explicit control grant or ownership token.** VEHICLE_STATE role is self-declared, "second to get in is the passenger" is decided locally on each machine from what it has heard, so two machines can disagree on who drives after a race (both enter in the same 100 ms).
3. **No dedicated enter/exit events.** Entering and leaving are inferred from reports starting and stopping (500 ms timeout); an exit shows up half a second late and a brief packet loss looks like an exit.
4. **No correction policy beyond extrapolation.** Nitrox sets velocity to close the gap and snaps on large errors; DS2 sets a transform plus velocity each tick (`placeEntity`). A snap threshold and a smoothing rule are not written down.
5. **Heartbeat/threshold behaviour.** DS2 sends 30 Hz continuously while driving. Nitrox sends only when moved and uses a heartbeat; useful for parked vehicles with a driver idling.

### Concrete changes
- Add reliable `VEHICLE_ENTER {vehicle id, seat, seq/epoch}` and `VEHICLE_EXIT {vehicle id, epoch}` messages; keep the 30 Hz stream for motion only. The 500 ms timeout stays as a fallback.
- Let the **host decide the driver** (it already is the world server): the first ENTER for a vehicle wins the driver seat, later ones become passengers, and the answer is sent back with an epoch. This is the exclusive-lock idea in Nitrox reduced to one message.
- Queue ride requests per vehicle id until the local copy is loaded and its mover exists; flush on load. Reuse `ds2::loadedVehicle`; poll it each tick with a 5 s timeout and a toast on failure rather than a log line.
- Write the correction rule explicitly: velocity-correct under N metres, snap above (Nitrox uses 20 m), and send only when moved or every 1 s while parked.
- Add the vehicle's seat id (which door or pod) to ENTER so a passenger's seat is chosen by the sender, not guessed.

---

## 4. Ownership and authority handoff

### How they do it
**Tilted.**
- Server data per actor: `OwnerComponent {owner pointer, ownership epoch (starts at 1), InvalidOwners list}`. Client data: `LocalComponent {server id, epoch, last sent dead/weapon flags}` for actors it owns, `RemoteComponent {server id, cached form id, epoch}` for the rest.
- Events that move ownership: owner relinquishes (cell change away, unload), owner leaves, owner "declines" a grant, a leader claim, a mount. Server picks the **next candidate among players who can see the actor**, skipping `InvalidOwners` (cleared on a voluntary relinquish, appended to on decline), so unloaded clients cannot bounce ownership back and forth.
- Every state-carrying message carries `(server id, epoch)`. Receivers drop anything whose epoch does not match their current record: inventory, equipment, death state, release, claim, rider/mount pairs (both epochs checked). Releases with an unknown reason are ignored. A claim presents the *expected* epoch.
- Client side of a transfer: ignore if the epoch is not newer; if the new owner is me, refuse (decline) when the actor or its 3D is not ready; otherwise flip the actor to "remote" **first**, reconcile it to the server's canonical actor data while still treated as remote, then install the local component and only then clear the remote flag. If the new owner is someone else, drop the local components and add remote ones.
- Reconcile rule: apply canonical values, inventory (only if different), dead/alive. For the *local owner* never resurrect an actor that died while the request was in flight (a respawn re-rolls and re-creates the actor, which the engine then rediscovers as a new entity forever); keep the local death and let the normal death report correct the server.

**Nitrox.** Server holds `playerLocksById: id -> (player, lock type)`. Acquire rules: no lock, or same player, or the holder has a transient lock and the requester wants exclusive. Everything else fails. A request produces a response to the requester and a change broadcast to everyone else. Revocation on cell leave (only if the player can no longer see it), on disconnect (all locks for that session), and on explicit drop. On a cell change the server computes, per player, the entities in the new cells that are worth simulating (whitelist), tries to give them transient locks, and for removed cells reassigns to other players. Client side, `SimulationOwnership` tracks locks by id, runs a callback when a lock request completes, and starts or stops local simulation of the entity.

**SkyMP.** The server keeps a `hosters` map (actor id to the hosting player's actor). Anything the client sends about a non-player actor is checked against it: if the sender is not the hoster the server logs, drops the message and sends the client a **host-stop** so it stops simulating that actor. Hits with a non-player aggressor must come from its hoster too.

**FiveM.** Entity owner follows the nearest player; scope enter/leave events let scripts react; hard rule never cache the owner.

### DS2 today
The host is the world server; the driver owns the vehicle and its load (declared by the sender). There is no epoch, no decline and no host-stop message in the wire contract I read (`CODEMAP.md` message list). Vehicle role is self-declared.

### Gaps and changes
- Introduce one small generic **authority record** used by vehicles now and enemies later: `{id, owner slot, epoch}` held by the host; messages about that id carry the epoch; the receiver drops mismatches. Add `AUTH_DECLINE {id, epoch}` (receiver cannot host it: not loaded, body not ready) and `AUTH_STOP {id}` (host tells a peer to stop acting on it). Both are about 8 byte messages.
- Order the guest-side flip like Tilted: mark remote, reconcile, then take control. For a vehicle this is: stop forwarding the host's state, snap to the owner's last state, only then let the local player's inputs drive.
- Do not resurrect on echo: the DS2 equivalent is not respawning a vehicle or enemy the owner already reported destroyed because of a stale snapshot; carry destroyed as an explicit state with the epoch.
- Whitelist what gets an authority record (vehicles, MULE and BT actors, loose cargo) and skip static scenery, as Nitrox does.

---

## 5. Enemy / NPC sync and combat: hits and death

### How they do it
**Tilted.** Damage is not computed centrally. The game applies a hit on whichever machine the hit lands in; a health-change event on that machine becomes `RequestHealthChangeBroadcast {server id, delta}`, relayed to the other machines in range, where the delta is **added** to the remote actor's health through a forced actor-value change. Deltas are used (not absolute values) so two machines hitting the same enemy compose. Changes smaller than one hit point are accumulated and flushed once per second. If an actor reaches zero health on a receiver it is killed there, except players, which are never killed by a remote message. In addition, the **owner polls the actor's dead flag every 250 ms** and sends a death-state change `(id, epoch, isDead)`; receivers apply it only if their epoch matches, and players are again exempt. Projectiles are relayed as a launch message (shooter, origin, angles, projectile, weapon, ammo, spell, flags like auto-aim and always-hit) that every other client replays through the game's own projectile launch so physics and visuals are local. Spawn data carries a dead flag, and a late joiner's reconcile kills or respawns to match.

**SkyMP.** The client reports a hit as `(aggressor, target, weapon source, flags: bash, power, sneak, blocked, projectile)`. The server validates in order: sender has an actor; aggressor is the sender (id 0x14 means "me") or an actor the sender hosts; target exists; both are in the same cell or world; distance below one exterior cell width unless it is a bow or crossbow shot; aggressor is alive (if dead the server triggers a respawn to repair the client's death state); the weapon or spell is in the aggressor's equipment or inventory. Then it computes damage with a pluggable formula (weapon base, armor reduction with the game's constants) and applies it to the target's authoritative health, which flows back as a property update.

**Kenshi (low confidence docs).** Server-authoritative damage, but NPC AI runs separately on each client; the authors list it as a known limitation, and a later authority design adds generation ids and an echo-suppressing validator.

### DS2 today
Roadmap item 8 (not started per the roadmap): host simulates enemies; planned reuse of RE0's enemy_net pattern. Guest hits forwarded to the host, enemy state broadcast.

### Concrete changes
- Use **additive health deltas with an epoch** between machines instead of absolute HP; it composes when both players shoot the same MULE. Batch tiny deltas for 1 s.
- Make **death an explicit reliable event**, polled at about 4 Hz on the owner, applied only on matching epoch; never inferred from HP on the receiver alone (a Tilted release bug was "enemies not dying at 0 HP").
- Validate guest hit descriptors on the host in the SkyMP order: sender owns the aggressor, target exists and is in the same area, plausible range, aggressor alive, weapon carried. Cheap and it makes the descriptor path safe against stale state.
- Replay projectiles (grenades, thrown items) through the game's own launch on the receiver rather than sending the resulting motion.
- Players are never killed by remote messages: for DS2 this means a remote body cannot be damaged or die from a message; only the real player on its own machine can.

---

## 6. Inventory and equipment, including drawn weapons on remote bodies

### How they do it (Tilted, the closest to our blocked problem)
- **A remote actor has a real inventory and a real equipment state.** Inventory changes on an owned actor produce `RequestInventoryChanges {server id, epoch, item entry, drop flag}`. On receipt the other machines call the game's own add/remove-item (or drop/pick-up) on the remote actor **inside a scoped override flag** that makes the mod's own hooks ignore those calls, so nothing echoes. Entries carry the full instance data (count, charge, enchantment effects, health, poison, soul, worn flags, quest-item flag); entries merge only when all extra data match.
- **Equipping is a first-class message**, not an inferred inventory diff: `{server id, epoch, item, slot, count, unequip, is spell, is shout, is ammo}`. The receiver calls the game's own equip manager on the remote actor: `Equip` / `UnEquip` with the slot form, spell and shout variants having their own calls. A quirk handled in the code: for armour the game does not auto-unequip what is worn, so the code first unequips everything worn, equips the new piece, then re-equips the rest. The sender also attaches the full equipment list to each request for later reconciliation.
- Messages carry the epoch and are dropped if ownership changed after they were queued.
- **Drawn weapon is separate state.** The owner polls "weapon drawn" every 500 ms and sends a draw request on change. On the receiver, the remote's flag is applied through a helper that drives the game's draw sequence, **applied twice** (after 0.5 s and again after 2 s) because the engine's draw sequence is unreliable when the actor's 3D has just loaded; the cached request is cancelled if the actor stops being remote or ownership arrives. Reconcile on ownership gain also re-applies the draw state for the local owner.
- Order that makes the held item appear in the hand: the item is already equipped on the remote (so the engine attaches its model), then the draw state plays the draw animation and moves the model from holster to hand. The mod never attaches a model to a hand node itself.

### DS2 today
`equip_sync.cpp` mirrors the four holster slots (kinds 4 to 7) by creating or deleting **baggage pieces** in the remote body's baggage owner. The header says a piece created in a hand slot (8 or 9) is ejected by the game within a second or two, so a drawn weapon is not mirrored. `ds2/equip_probe.cpp` (temporary) calls `DSPlayerSystem_sExportedEquipItem(item, mode)` for the local player and hooks the slot-removal function to see who ejects pieces.

### Why DS2 is stuck, in Tilted's terms
DS2's holster mirror is the equivalent of Tilted's *inventory* message. Tilted's held item works because of the second step: a call to the **engine's own equip routine on the remote actor**, followed by the **draw state**. DS2 adds a piece directly into the hand slot (a baggage-manager operation) without telling the player's weapon controller that it is equipped, so the engine's validation sees an owned piece in a hand slot that no weapon controller claims and removes it. That reading is a hypothesis; it matches the symptom (piece appears, then is ejected 1 to 2 s later).

### Concrete changes (ordered, each cheap to test with the existing probe)
1. **Find the ejector.** `equip_probe` already hooks the slot-removal function (`hookSlotRemove`). Log its caller chain when a created hand-slot piece is removed; the top frame names the validation that claims the piece. If it is the weapon controller or "equipped item" check, step 2 is the fix.
2. **Run the game's own equip flow for the remote.** Call `sExportedEquipItem` (or the wheel's Equip / Get Ready handler found in roadmap item 6c) **inside `remote_context::enter`**, the mechanism that already answers "the player with local index 0" for the remote body. The exported function takes an item id and a mode with no entity argument, which suggests it resolves the actor through that same lookup, so redirecting the lookup should equip on the remote. Validate by checking that the hand-slot piece survives past 2 s.
3. **Send equip as an explicit event** as Tilted does: `EQUIP_EVENT {epoch, item kind, slot, unequip, mode}` reliable, plus a periodic reconciliation (the existing 500 ms holster list). Keep the baggage-slot mirror only as the reconcile fallback.
4. **Send draw state separately** on change, polled at about 2 Hz, and apply it twice on the remote (immediately, then after about 1 s) to cover the engine's post-spawn flakiness, cancelling if the body is forgotten or respawned.
5. Add a scoped "applying remote state" flag that all of the adapter's own hooks check (equip, baggage events), so applying a partner's state never gets reported back. `world_facts` already suppresses its own writes when applying; make it one shared flag.

---

## 7. World state, quests and saves

### How they do it
**Tilted quests.** The client registers sinks for the game's own quest start/stop and quest-stage events (it deliberately chose the "stage" event because the "stage item done" event fires too late). Both handlers bail out when a scoped override flag is set (the flag is set while applying a remote update) or when the player is not in a party. Non-syncable quests are filtered by id, and quest types None and Miscellaneous are excluded unless an experimental server setting is on. The request carries `(quest id, stage, status started/stage/stopped)`; the server updates the sender's tracked quest log (adding the quest if it was not there so late updates still work), then forwards to the sender's party. Receivers apply with the game's own script-level set-stage, set-active and stop calls. Rules around it: the party leader holds the truth; changing leader breaks quests; quest-critical dialogue and loot are the leader's.

**Nitrox world.** Server keeps story goal data (a set), the PDA unlock data, the Aurora timeline (anchored to server time, adjusted when time is skipped) and a time service; story goals are sent by clients when executed (`StoryGoalExecuted`, `GoalCompleted` processors) and the full set is part of the join sync. Clock sync gives a per-client delta to the server clock using an NTP-corrected pair, so scheduled events happen at the same real moment.

**Seamless Co-op (secondary sources).** Progress is event flags; flags sync to the host; guests lose rewards of host-gated events; a community tool repairs inconsistent flags by id.

**Saves.** Nitrox: server holds the world. cyber.rest: host store plus GUID identity per player. Tilted: each player keeps their own save, the leader's world is what is shared live.

### DS2 today
`world_facts.cpp` hooks the FactDatabase writers (bool and int), queues each changed fact (one entry per fact, last value, only during host gameplay), `fact_sync.cpp` sends FACT_SET (reliable, 16-byte UUID, value) host to all; the guest applies through the game's own writer (so the hook does not re-fire) and keeps entries pending until the database is found. Saves: session Documents redirect plus host-to-guest save copy on join.

### Comparison and changes
The DS2 design is the same shape as Tilted's quest sync with a cleaner hook point (the fact writers are below quest, mission and flags together). Differences worth acting on:
1. **No join snapshot of facts.** I found no full dump on join in `fact_sync.cpp`: only deltas after the link is up, plus the saved copy at the time of the host-to-guest save transfer. A guest that joins late, reconnects, or loads after the host has progressed since its save copy misses earlier changes. Nitrox sends the whole story set with the join sync. Change: on peer-up and on a guest's `gameplay started` event, the host walks the fact database once (or keeps a "changed since session start" set, which is simpler and bounded) and sends it in chunks of the existing `kMaxEntries`.
2. **No scoped "applying remote" guard other than calling the original writer.** Fine today, but when orders are opened to the guest, the guest's own writes will need to flow guest to host; the scoped-override pattern is what keeps that from echoing. Build the one shared flag now (section 6, change 5).
3. **Filter list.** Tilted keeps an explicit non-syncable quest list. DS2 should keep a list of fact name prefixes known to be local-only (UI, tutorials, per-player stats) and exclude them; log which ones change most to build it.
4. **Order of application.** Facts written while the guest's world is loading can be overwritten by the load itself; the current pending queue handles "database not found", but not "database found but world still loading". Gate application on the same gameplay gate used for the body.
5. **Scheduled world events** (weather, timefall) should use the clock-offset approach in section 8.

---

## 8. Desync detection and recovery

### How they do it
- **Epoch checks** everywhere (Tilted) drop messages from a superseded owner; Kenshi's authority validator had eight outcomes (apply remote, reconcile local echo, queue until spawned, reject wrong owner, reject stale generation, reject destroyed, reject unknown) and counted them.
- **Pending queue until spawned** (Kenshi): updates for entities that don't exist yet are held and applied at spawn, expiring after 10 s.
- **Server-canonical reconcile on ownership events** (Tilted): the server stores the actor's last known values/inventory/dead flag and re-applies them whenever an actor changes hands or is spawned for a viewer.
- **Explicit stop** (SkyMP host-stop) and **decline** (Tilted) for authority conflicts.
- **Clock sync** (Nitrox): each side corrects its UTC against an NTP source if reachable, a ping-based procedure measures client to server delta, and server time is derived as `client time plus delta`. Replicators timestamp snapshots with the sender's real-time clock.
- **Adaptive latency buffer** (Nitrox): a single scalar per replicator, bumped immediately on a larger measured latency plus a safety margin, lowered only if the recent maximum is well under it, evaluated every few seconds.
- **Position error policy:** velocity-correct, snap beyond 20 m (Nitrox); NaN guard.
- **Validation of sender identity:** BeamMP rejects position packets whose declared owner id is not the sending connection.
- **Manual repair** (Seamless): a save/flag repair tool, because automatic recovery was never built.
- Nobody uses periodic full-state hashing for these single-player engines.

### DS2 today
Sequence numbers with restart detection for PLAYER_STATE and VEHICLE_STATE, stale timers (3 s and 500 ms), full anim snapshot each second, periodic CARGO_LIST and VEHICLE_LOAD re-sends (5 s), host-decided cargo moves with the host accepting only kinds it asked for. RTT stats exist in the launcher; clock offset is listed as planned in `FINDINGS.md`.

### Concrete changes
1. **Clock offset first.** Add the sender's synchronised timestamp to PLAYER_STATE, VEHICLE_STATE and ANIM_STATE (sections 2 and 3 depend on it). Use ping/pong samples, keep the median of the last N, no NTP needed on a friends-only link.
2. **Adaptive delay** per remote stream (Nitrox pattern) rather than a fixed delay.
3. **Pending queue until spawned**, 10 s expiry, for any state addressed to a body, vehicle or cargo piece not yet created on this machine (vehicle ride requests, cargo drops already do a version of this with "placed once the receiver is near").
4. **Resync commands** on both machines (key or console): resend my full snapshot (anim, equip, cargo, vehicle) and request the peer's; log what differed. This is the manual repair tool, built in from the start. Use it also automatically when a reject counter passes a threshold.
5. **Reject counters** per message type (stale epoch, unknown id, wrong sender) printed in the 5 s stats line already used by `anim_sync`.
6. **Sender identity from transport** is already how the launcher works (identity fields come from the link, not the payload), so nothing to add beyond keeping it that way for any new messages.

---

## Summary of changes for DS2 (by effort and payoff)

| # | Change | Section | Effort | Unblocks |
|---|---|---|---|---|
| 1 | Find who ejects hand-slot pieces (probe), then run the game's equip flow on the remote under `remote_context`, plus a separate draw state applied twice | 6 | small to medium, one probe session | drawn weapons on the remote body (roadmap 6c) |
| 2 | Timestamp + clock offset, shared delay line for position and animation, adaptive delay | 2, 8 | medium | smooth bodies, aligned poses |
| 3 | Reliable ENTER/EXIT and host-decided driver with epoch; ride requests queued until the vehicle copy loads | 3, 4 | medium | reliable seating (roadmap 1, 4) |
| 4 | Tracked / Parked / Placed body states with a ground-loaded check; spawn catch-up order | 1 | medium | area transitions, no falling bodies |
| 5 | Authority record `{id, owner, epoch}` with DECLINE and STOP messages, used by vehicles first, enemies next | 4 | small spec, medium code | enemy authority (roadmap 8) |
| 6 | Reliable or repeated pulse-like animation edges | 2 | small | thrown/stumble/pickup motions never lost |
| 7 | Additive health deltas + explicit death event with epoch; host-side hit validation in the SkyMP order | 5 | medium | combat results counted on both sides |
| 8 | Facts snapshot on join and on guest gameplay start; shared "applying remote" flag; local-only fact filter | 7 | small | rejoin, mid-session guest |
| 9 | Resync command, reject counters, pending queue with expiry | 8 | small | desync recovery, test harness diagnostics |
