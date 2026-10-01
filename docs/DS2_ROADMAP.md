# DEATH STRANDING 2 co-op roadmap

Target architecture: **the host is the world server** (`DS2_NOTES.md`, "Target design"). The guest is an ally: they carry, fight and pick up, but never decide world state. Every stage is tested with a fake peer on one PC first, then on two PCs.

## End goal (the user's wishlist)
1. Your inventory and cargo stay yours. Each machine runs its own Sam (backpack, rack, equipment, weight, balance), and the partner never edits them.
2. The partner's cargo is shown, not simulated: a visual mirror on their body, sent on change, with no physics or weight.
3. Ground cargo is shared. Drop, pickup and terminal actions run on the actor with the game's own calls, then replay on the other machine. A handoff is a drop plus a pickup. The driver owns the vehicle and its load.
4. World progress is the host's: orders, deliveries, facilities, the Chiral network, structures and roads. Either player can deliver, and credit goes to the deliverer. A shared locker is host-tracked; private lockers stay per player.
5. Saves: the host's save holds the world, and personal gear stays on each side. Rejoining gets the current world state.

## Stage A: two players in one world (now)
- **Players get:** a visible second Sam (or humanoid) moving with the partner; the guest loads the host's world from a session copy of the host's save; the guest cannot use terminals or trigger orders or quests; the host gives items to and takes items from the guest through an adapter-drawn menu; guest pickups only complete after the host confirms.
- **Done so far:** position link (PLAYER_STATE 60 Hz), labelled marker; a borrowed humanoid NPC walks as the partner's body; session saves (Documents redirect) and host-to-guest save sync; the guest gate (only terminals and story triggers, i.e. sequence-network use locations, are refused); the host's give/take menu (F7), checked both ways with a fake guest and a fake host (CARGO_LIST / CARGO_TAKE / CARGO_ADD, 0x0101-0x0103); guest pickups of world cargo confirmed by the host (CARGO_PICKUP / CARGO_PICKUP_RESULT, 0x0104-0x0105: the host deletes its copy or the guest's pickup is undone), checked on both sides with fakes.
- **Learn:** the humanoid spawn path, brain switch-off, the inventory add/remove calls, the interaction/trigger check, save location and slot handling.
- **Exit:** fake peer: two bodies on screen, a scripted peer walk drives the second body, the menu moves an item both ways, a blocked terminal on the guest, a pickup held until a fake host confirms. Two PCs: the same with real players.
- **Risks:** no callable spawn (script exports are stubs); a body's own AI fighting our transform; saves in the Steam cloud need a session redirect like RE0's.

### Two-PC test checklist (Stage A)
Setup, on both PCs:
- The same commit of this repo, the launcher built (`launcher/bin/...`) and `adapters/ds2/version.dll` built. The launcher copies `version.dll` into the game folder when it starts the game, and the adapter writes `coop\adapter.ini` on first start.
- In the game's graphics options, turn frame generation off (DLSS / FSR / XeSS frame generation) before the session. The overlay draws on the game's swap chain and is only tested without it.
- To see the partner as a body as well as a marker, set `remote_body=1` in `<game>\coop\adapter.ini` (after the first start) and restart the game.
- Back up `Documents\DEATH STRANDING 2 - ON THE BEACH\<steamid>` on both PCs.

Run:
1. Host: start the launcher, pick DEATH STRANDING 2, click Host, send the lobby code. Guest: paste the code, click Join. The guest receives the host's saves (`coop\session\Documents\...`), and its game plays that copy.
2. Both: start the game and load (Continue). Each sees the other's name marker above their position; with `remote_body=1`, a borrowed NPC walks there.
3. Guest: walk to a terminal. The "Activate Terminal" prompt must not appear, and F must do nothing. Vehicles, cargo pickup and Cargo Management still work.
4. Host: F7 opens the give/take menu. Give one piece (arrow keys, Enter); it must leave the host's backpack and appear in the guest's (check the guest's Cargo Management). Take one piece back the same way.
5. Guest: pick up loose cargo that both worlds have (e.g. lost cargo near the start). The piece stays, and the same piece must vanish from the host's world. Then the host picks up another loose piece; it must vanish from the guest's world.
6. Guest: offload a piece in Cargo Management (Ring Menu, Cargo Management, the piece, Offload); the same piece must appear at that spot in the host's world. The host then offloads one; it must appear in the guest's world. Either player picks one of them up; it must vanish from the other world.

Collect from both PCs: `<game>\coop\adapter.log`, the launcher's console output, and any `<game>\coop\crash-*.dmp`. The host also receives the guest's log and dumps as `<game>\coop\peer_*`. Note what you saw at each step, with screenshots of anything off.

To play vanilla afterwards: delete `version.dll` and `coop\` from the game folder.

## Stage B: cargo you can see and share (wishlist 2, 3)
- **Done so far:** loose world cargo is the same in both worlds: pickups (the guest's confirmed by the host, the host's mirrored to the guests) and drops (placed at the same spot in the other world once its player is near); fake peers both ways. The partner's load is drawn on their body (one box per piece in their backpack, two wide up the back, updated within 0.5 s of a change); an overlay, so it shows through walls. Vehicles: the driver's vehicle moves in the other world (VEHICLE_STATE, 30 Hz), stays where it is left, and a player's own vehicle is never moved by the partner; the driver's vehicle bed is mirrored too (VEHICLE_LOAD, by kind); the partner's body waits at its home spot while they drive.
- **Players get:** the partner's load shown on their body; ground cargo drop/pickup/handoff in sync; vehicles driven by their owner with their load.
- **Learn:** cargo item identities across machines, the drop/pickup entry points, vehicle ownership and seats.
- **Exit:** fake peer replays a drop and a pickup to the same item on the ground; mirror updates within one second of a rack change; two-PC handoff of one crate.
- **Risks:** item ids that are per-machine (need a host-issued id like RE0's floor items); physics-simulated cargo drifting apart.

## Stage C: one shared world (wishlist 4)
- **Players get:** structures, roads, orders and deliveries (credit to the deliverer), facility connections, the Chiral network and a shared locker, all following the host.
- **Learn:** the world-state managers and which state is a flag set versus objects; structure build and damage events.
- **Exit:** fake host builds and destroys a structure and completes an order, and the guest shows it; two-PC delivery by the guest credited to the guest.
- **Risks:** structure state tied to the online Social Strand system; order scripts that assume one player.

## Stage D: persistence and the last restrictions (wishlist 5)
- **Players get:** rejoin into the current world; personal gear saved on each side; guest deliveries allowed; the host's Social Strand content mirrored to the guest.
- **Learn:** which strand content lives in the save and which is fetched per account online.
- **Exit:** a guest rejoins mid-session and sees the current world; a guest delivery saves correctly on both sides; the guest's own strand fetch is off during co-op.
- **Risks:** online strand content tied to the PSN/Steam account (riskiest piece, last on purpose).
