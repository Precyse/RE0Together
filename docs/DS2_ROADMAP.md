# DEATH STRANDING 2 co-op roadmap

**Goal (the user's words):** "It should be just as if they're playing single player but with a friend. Nearly everything the host does, the partner should be able to do (minus turning in missions, for sync purposes, for now)." Seating in vehicles must be reliable.

Architecture: **the host is the world server** (`DS2_NOTES.md`, "Target design"). The guest does host-equivalent actions; anything that changes the shared world (structures, roads, placed items, combat results, world progress) is carried out through the host, so a guest action becomes a host-confirmed world change rather than a local-only one. The one thing the guest cannot do yet is accept or turn in orders. Every item is tested with a fake peer on one PC first, then on two PCs.

## End goal (the user's wishlist)
1. Your inventory and cargo stay yours. Each machine runs its own Sam (backpack, rack, equipment, weight, balance), and the partner never edits them.
2. The partner looks and moves like a second player: a Sam-like body with the owner's real animations, their real rack with the real cargo models, seated in vehicles like the player.
3. Ground cargo is shared. Drop, pickup and terminal actions run on the actor with the game's own calls, then replay on the other machine. A handoff is a drop plus a pickup. The driver owns the vehicle and its load; the other player can ride along.
4. World progress is the host's: orders, deliveries, facilities, the Chiral network, structures and roads. The guest builds, repairs and fights like the host, through the host. Credit for a delivery goes to the deliverer (once guest deliveries open). A shared locker is host-tracked; private lockers stay per player.
5. Enemies (MULEs, bandits, BTs, other hostile AI) react to both players; the host simulates them.
6. Saves: the host's save holds the world, and personal gear stays on each side. Rejoining gets the current world state.

## Done (Stages A and B)
- **Stage A, two players in one world:** position link (PLAYER_STATE 60 Hz), labelled marker; a borrowed humanoid NPC walks as the partner's body; session saves (Documents redirect) and host-to-guest save sync; the guest gate (sequence-network use locations refused: terminals and story triggers); the host's give/take menu (F7, CARGO_LIST / TAKE / ADD 0x0101-0x0103); guest pickups confirmed by the host (0x0104-0x0105). Fake-peer exit met; two-PC run pending (checklist below).
- **Stage B, cargo you can see and share:** loose world cargo is the same in both worlds: pickups (guest's confirmed by the host, host's mirrored, 0x0106) and drops (0x0107, placed once the receiver is near); the partner's load drawn as boxes on their body (overlay, stopgap until item 7); the driver's vehicle moves in the other world (VEHICLE_STATE 0x0108), stays where it is left, its bed is mirrored by kind (VEHICLE_LOAD 0x0109). Stopgap: the partner's body waits at its home spot while they drive (until item 1). Fake-peer exit met; two-PC crate handoff pending.

## Order of work (parity)
Each item lists what players get and its exit tests. Fake-peer tests first, then two PCs.

### 1. Reliable vehicle seating (now)
- **Players get:** the partner visibly sits in the driver's or passenger's seat with the game's own seated / driving pose, follows the vehicle exactly, and gets in and out with the game's own enter and exit; no hidden or floating body.
- **Learn:** how the game seats Sam: the player-in-vehicle states (DSPlayerVehicleRideOnState, DSPlayerVehicleDriveState, DSPlayerVehicleRideOffState, passenger states), the seat attach on the vehicle entity, the animation states used. Whether a borrowed NPC can take them; if not, build the proper remote body (item 5) here.
- **Exit:** fake peer: enter, drive, exit and re-enter five times without a body left behind, misplaced or stuck; the body stays in the seat at speed and on slopes; screenshots of the partner seated. Two PCs: both players in one vehicle, driver and passenger, in both directions.

### 2. The guest's terminals: everything but orders
- **Players get:** the guest uses terminals for everything (private room, lockers, fabrication, Cargo Management at terminals, upgrades, mail, ...) except accepting and turning in orders; refusing those shows a toast.
- **Learn:** the order transactions behind the terminal menu (accept, deliver / turn in) and the point to refuse them; the narrowed gate replaces the sequence-network refusal.
- **Exit:** fake host session: the guest opens a terminal, uses the private room, a locker and fabrication; accepting an order and delivering are refused with a toast and nothing changes in either world; the host still does both.

### 3. Shared world through the host (Stage C, reordered)
- **Players get:** the guest builds structures and roads, repairs, places items, and its combat results count, all as host-confirmed world changes; the host's structures, roads, orders, facilities and Chiral network show in the guest's world.
- **Learn:** the world-state managers (`DS2_NOTES.md`, "Stage C map": FactDatabase, DSConstructionManager, DSRoadManager, DSNetRoadSyncManager, DSMissionSystem); which progress is facts and which is objects; structure create / destroy / damage.
- **Exit:** fake host builds and destroys a structure and completes an order, and the guest shows it; a fake guest builds a structure and the host creates it (and refuses one it cannot place); a road repair by the guest appears in both worlds.

### 4. Riding with the host
- **Players get:** both seats work in either direction; the passenger's view rides along with the vehicle the partner drives; the driver owns the vehicle, its physics and its load, the passenger rides it.
- **Learn:** the passenger state for the local player in a vehicle moved by the partner's reports (the local vehicle copy must stay kinematic under the passenger), the passenger camera.
- **Exit:** fake driver drives a loop with the local player as passenger: the local Sam stays seated, the camera follows smoothly, getting out puts him beside the vehicle; then the roles swap.

### 4b. Warp to the partner
- **Players get:** a menu action that moves the local Sam next to the partner, for when the two drift far apart (the idea comes from the Clair Obscur co-op mod's "teleport to host").
- **Learn:** a safe player teleport across unloaded areas (`Entity::PlaceOnWorldTransform` plus waiting for streaming), refused while riding, carrying a stack mid-fall or in a cutscene.
- **Exit:** guest 2 km from the host warps beside them, lands on loaded ground, and keeps their cargo.

### 5. The partner's body as a real second player
- **Players get:** a Sam-like body (Sam's model, or a Sam-like porter) instead of whichever NPC is nearest, present everywhere, not only where NPCs are loaded.
- **Learn:** a body that runs the player's own animation graph and vehicle states: the player entity resource, or a porter NPC resource with the player's animation set; the spawn setup that faulted before (`DS2_NOTES.md`, "In-world remote body").
- **Exit:** the body appears at the partner in an empty area (no NPC nearby), looks like Sam / a porter, and survives area changes and the partner leaving and rejoining.

### 6. Animation mirroring
- **Players get:** the partner's body plays the owner's real animation state, not walk / idle from velocity: run, balance and stumble, fall, climb, ladder, rope, crouch, aim, throw, combat moves, cargo pickup and drop motions, carrying poses.
- **Learn:** the player's animation state machine (states and graph parameters) and how to drive a remote body's animation graph with them, as the RE4R motion-layer approach does (`FINDINGS.md`).
- **Exit:** fake peer replays a recorded sequence (walk, run, stumble, ladder up, crouch, aim, throw, pick up, put down) and the body plays each recognisably; two PCs: each player's moves show on the other screen within a few frames.

### 6b. The NPC system and shared animations
- **Players get:** poses the player network lacks (a true passenger pose, as MULEs and porters ride) on the partner's body, and NPC bodies that can play Sam's moves where the game needs a stand-in.
- **Learn:** how NPCs are spawned (spawn setups, encounters, the AI ride mover and `AIRiderPosture`), how the NPC animation network differs from the player's (`tools/ds2/out/analysis/ANIMATION.md`), and whether an animation network or its clips can be swapped or shared between a player entity and an NPC body (NPC clips on Sam, Sam's clips on an NPC).
- **Exit:** the remote player body plays the MULE passenger pose in the passenger seat; an NPC body plays a recorded sequence of Sam's moves recognisably.

### 6c. The partner's held equipment
- **Players get:** what the partner draws from the weapon wheel (weapon, grenade, consumable, tool) is in their hand on their body, the same model in the same hand, and goes away when they holster it.
- **Learn:** how the game attaches the drawn item's model to Sam (the equipment component, the attach joint, the item resource id), the wheel's Equip / Get Ready call as the hook point, and the call that equips an item on another entity.
- **Exit:** single PC with the animation mirror on (`mirror_animation=1`): Sam draws a weapon, a grenade and a consumable from the wheel (key 1) and the body beside him holds the same models, A/B screenshots with and without the equip message; then through a fake peer (EQUIP_STATE, reliable, item id plus slot, sent on change).

### 7. Cargo stacking with the real models
- **Players get:** the partner's real rack layout (stack order, back, sides, hands, legs) with the real cargo models on their body, replacing the overlay boxes; the partner organises their own cargo in Cargo Management as in single player and the mirror follows.
- **Learn:** how the game attaches carried pieces' models to Sam (slot attach points per slot kind) and whether a remote body can carry model-only copies (no weight, no physics).
- **Exit:** fake guest with a recorded rack (back stack, side, hand, legs): the body shows each piece's model in place; a reorder or offload on the owner's side shows within one second.

### 8. Enemies
- **Players get:** MULEs, armed bandits, BTs and other hostile AI react to both players; hits on either side count; being grabbed and knocked down, cargo stolen by MULEs, timefall and BT encounters work for both.
- **Learn:** the enemy managers and their targeting; RE0's enemy_net / enemy_state pattern: the host simulates enemies, guest hits are forwarded to the host, enemy state is broadcast.
- **Exit:** fake guest stands near a MULE camp: the MULEs detect and chase the guest's body; a guest hit (scripted) damages the enemy on the host; a MULE stealing the guest's cargo shows on both sides; a BT area triggers for the guest.

### 9. Persistence and the last restrictions (Stage D)
- **Players get:** rejoin into the current world; personal gear saved on each side; guest order acceptance and deliveries (credit to the deliverer); the host's Social Strand content mirrored to the guest.
- **Exit:** a guest rejoins mid-session and sees the current world; a guest delivery saves correctly on both sides; the guest's own strand fetch is off during co-op.
- **Risks:** online strand content tied to the PSN/Steam account (riskiest piece, last on purpose).

## Two-PC test checklist (Stages A and B)
Setup, on both PCs:
- The same commit of this repo, the launcher built (`launcher/bin/...`) and `adapters/ds2/version.dll` built. The launcher copies `version.dll` into the game folder when it starts the game, and the adapter writes `coop\adapter.ini` on first start.
- In the game's graphics options, turn frame generation off (DLSS / FSR / XeSS frame generation) before the session. The overlay draws on the game's swap chain and is only tested without it.
- The partner's body is on by default (`remote_body=0` in `<game>\coopdapter.ini` turns it off; the file is written by the first start).
- Back up `Documents\DEATH STRANDING 2 - ON THE BEACH\<steamid>` on both PCs.

Run:
1. Host: start the launcher, pick DEATH STRANDING 2, click Host, send the lobby code. Guest: paste the code, click Join. The guest receives the host's saves (`coop\session\Documents\...`), and its game plays that copy.
2. Both: start the game and load (Continue). Each sees the other's name marker above their position; with `remote_body=1`, a borrowed NPC walks there.
3. Guest: walk to a terminal and hold F: the Delivery Terminal opens with all its menus. Take On Orders opens, but selecting an order shows the toast "Only the host can accept orders" and nothing changes (In Progress stays x0). With cargo for an order (carried over from the host's save), Deliver and Report must refuse with "Only the host can turn in orders" (UNVERIFIED so far: never exercised on one PC). Vehicles, cargo pickup and Cargo Management still work.
4. Host: F7 opens the give/take menu. Give one piece (arrow keys, Enter); it must leave the host's backpack and appear in the guest's (check the guest's Cargo Management). Take one piece back the same way.
5. Guest: pick up loose cargo that both worlds have (e.g. lost cargo near the start). The piece stays, and the same piece must vanish from the host's world. Then the host picks up another loose piece; it must vanish from the guest's world.
6. Guest: offload a piece in Cargo Management (Ring Menu, Cargo Management, the piece, Offload); the same piece must appear at that spot in the host's world. The host then offloads one; it must appear in the guest's world. Either player picks one of them up; it must vanish from the other world.
7. Either: drive a vehicle; it must move in the other world and its bed contents must match.
8. Order turn-in after a give and return (needs a delivery by hand): host takes a short order with a close destination, gives one of its order pieces to the guest and takes it back (F7), then delivers it at the terminal or destination; the delivery must count and the hand-over menu must list the piece under the order. Also check that the guest's own copy of the piece (a locker) is gone after the give, and that nothing is duplicated.

Collect from both PCs: `<game>\coop\adapter.log`, the launcher's console output, and any `<game>\coop\crash-*.dmp`. The host also receives the guest's log and dumps as `<game>\coop\peer_*`. Note what you saw at each step, with screenshots of anything off.

To play vanilla afterwards: delete `version.dll` and `coop\` from the game folder.
