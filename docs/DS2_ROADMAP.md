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
- **Done so far:** position link (PLAYER_STATE 60 Hz), labelled marker; a borrowed humanoid NPC walks as the partner's body; session saves (Documents redirect) and host-to-guest save sync; the guest gate (every use-location claim refused for now); the host's give/take menu (F7), checked both ways with a fake guest and a fake host (CARGO_LIST / CARGO_TAKE / CARGO_ADD, 0x0101-0x0103); guest pickups of world cargo confirmed by the host (CARGO_PICKUP / CARGO_PICKUP_RESULT, 0x0104-0x0105: the host deletes its copy or the guest's pickup is undone), checked on both sides with fakes.
- **Learn:** the humanoid spawn path, brain switch-off, the inventory add/remove calls, the interaction/trigger check, save location and slot handling.
- **Exit:** fake peer: two bodies on screen, a scripted peer walk drives the second body, the menu moves an item both ways, a blocked terminal on the guest, a pickup held until a fake host confirms. Two PCs: the same with real players.
- **Risks:** no callable spawn (script exports are stubs); a body's own AI fighting our transform; saves in the Steam cloud need a session redirect like RE0's.

## Stage B: cargo you can see and share (wishlist 2, 3)
- **Done so far:** pickups of loose world cargo are the same in both worlds (the guest's confirmed by the host, the host's mirrored to the guests; fake peers both ways). Drops are not mirrored yet.
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
