#pragma once
// The engine-specific layer: everything the co-op code needs from the running game. One implementation per game
// build (src/ds2/game.cpp for DEATH STRANDING 2); the rest of the adapter only talks to this interface.
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "combat_wire.h"
#include "env_wire.h"
#include "fact_wire.h"
#include "camp_wire.h"
#include "enemy_wire.h"
#include "story_wire.h"
#include "struct_wire.h"
#include "weapon_wire.h"
#include "world_to_screen.h"

namespace game {

struct Pose {
    world_to_screen::Vec3 position;  // world metres, Z up
    float yaw = 0;                   // radians about the up axis
};

// Finds the engine objects in the running image. False while the game has not built them yet (retry later) or on
// an unsupported build.
bool resolve();

// The local player character (Sam).
std::optional<Pose> localPlayer();

// The camera the game renders from.
std::optional<world_to_screen::Camera> camera();

// A function the game's simulation thread calls every frame, taking one pointer and returning one
// (DS2: Player::GetLastActivatedCamera); 0 before resolve() succeeds.
uintptr_t frameFunction();

// Starts following which use locations are driven by the story's sequence networks (terminals, order and quest
// triggers). Call once at start-up, before the world loads, so none is missed. False on an unsupported build.
bool watchInteractions();

// Guest restriction: while true the local player cannot claim sequence-network use locations, so the host alone runs
// the world's progress; vehicles, cargo and other interactions stay usable. Needs watchInteractions(). Any thread.
void blockScriptedInteractions(bool block);

// Guest restriction on orders: hooks the terminal menus' accept and turn-in callbacks (once, at start-up), and while
// `block` is true they refuse with a toast. Terminals themselves stay usable. Any thread.
void watchOrders();
void blockOrders(bool block);

// Opaque handle of an entity the adapter created; 0 = none.
using Body = uintptr_t;

// Moves a body (the engine's SetWorldTransform) and gives its mover the velocity it is moving with, so its own
// animation can follow. False when it faults.
bool placeBody(Body body, const Pose& pose, const world_to_screen::Vec3& velocity);

// A vehicle and where it is. The id is the game's own, saved with the world, so the host's and the guest's copies of
// one vehicle share it.
struct VehiclePose {
    uint64_t id = 0;
    world_to_screen::Vec3 position;
    float rotation[3][3] = {};  // rows: right, forward, up
};

// The vehicle the local player is driving now, where it is. Any thread.
std::optional<VehiclePose> drivenVehicle();

// Moves this world's copy of the vehicle `pose.id` there, with the velocity it moves at so its physics can follow.
// False when no such vehicle is loaded or the move faulted. Simulation thread only.
bool placeVehicle(const VehiclePose& pose, const world_to_screen::Vec3& velocity);

// One piece of cargo the local player carries.
struct Cargo {
    uint64_t handle = 0;  // this machine's id of the piece (what removeCargo takes)
    uint32_t type = 0;    // the kind of cargo, the same on every machine (what addCargo takes)
    std::string name;     // its display name in the game's language
    // The order the piece belongs to (DSBaggage +0x28 and +0x30, 0 for plain cargo) and what the hand-over menu and
    // damage need. A piece handed to the partner and back is recreated with the same ids, so the order still counts it.
    uint64_t orderId = 0;
    uint64_t secondId = 0;
    uint8_t category = 0;
    float durability = 0;
};

// Whether a value of DSBaggage +0x28 / +0x30 is an order id (a mission number and an order type); anything else is plain cargo.
bool isOrderId(uint64_t id);

// What the local player's backpack holds: cargo, and weapons and tools stowed in it, but not the equipped gear.
// Any thread.
std::vector<Cargo> carriedCargo();

// A piece lying loose in the world (on no one's rack, in no locker or vehicle).
struct LooseCargo {
    uint64_t handle = 0;
    uint32_t type = 0;
    world_to_screen::Vec3 position;
    uint64_t orderId = 0;  // the order the piece belongs to (0: plain cargo), the same in both worlds
};

// Loose pieces within `radius` metres of `around`. Any thread.
std::vector<LooseCargo> looseCargo(const world_to_screen::Vec3& around, double radius);

// The handle of the piece of this order (an order id, as in Cargo::orderId) that this world holds anywhere outside the
// local backpack (a locker, a shelf, the ground): order pieces are matched by identity, not by where they lie.
std::optional<uint64_t> findOrderPiece(uint64_t orderId);

// What addCargo did: made the request, must be asked again shortly (a stale copy of an order piece was being removed
// first), or will never work (the backpack already holds that order piece, the game refused).
enum class AddResult { Done, Retry, Refused };

// Asks the game to create `piece` (its kind, and for order cargo its order link) on the local player's backpack. Plain
// cargo goes through the game's own request queue, order cargo through the manager's create with the link copied;
// both are served on the game's next update. An order piece whose id this world still holds elsewhere (a locker: both
// worlds start from one save) has that stale copy removed first and answers Retry. Any thread.
AddResult addCargo(const Cargo& piece);

// What the bed of vehicle `vehicle` (a VehiclePose id) holds in this world; empty when it is not loaded. Any thread.
std::vector<Cargo> vehicleCargo(uint64_t vehicle);

// Asks the game to create a piece of `type` in the bed of vehicle `vehicle`. False when that vehicle is not loaded
// or the game refused. Any thread.
bool addVehicleCargo(uint64_t vehicle, uint32_t type);

// What the backpack of the player whose baggage owner has this key holds (a remote body's network id), and a new piece
// created into that backpack's main load: how the partner's rack is shown on the body. An order piece keeps its link and
// a copy of it that this world holds elsewhere is removed first (Retry). Both refuse an owner that is part of the local
// player's tree. Any thread.
std::vector<Cargo> backpackCargo(uint64_t playerKey);
AddResult addBackpackCargo(uint64_t playerKey, const Cargo& piece);

// Marks the baggage owner with this key (a remote body's network id) and its child owners active or not. An inactive
// owner is skipped by the cargo menus' gather, so a partner's rack is not listed as the local player's. Refuses the
// local player's own key. Any thread.
void setOwnerActive(uint64_t ownerKey, bool active);

// The address of the baggage owner with this key (0 = the local player's), 0 when there is none. Any thread.
uintptr_t baggageOwner(uint64_t ownerKey);

// What the game moved between the partner's rack (the remote body's baggage owner) and this world since the last call: the
// partner's pieces that went into a terminal (delivered) or into the local player's own tree (taken), which the partner's
// machine must delete; and the local player's pieces that went into the partner's rack (given), which it must create.
struct PartnerMoves {
    std::vector<Cargo> left;
    std::vector<Cargo> given;
};

// Any thread.
PartnerMoves takePartnerMoves();

// What one slot kind of the baggage owner `ownerKey` holds (0 = the local player, a vehicle's id, a remote body's
// network id); empty when there is no such owner. Any thread.
std::vector<Cargo> slotPieces(uint64_t ownerKey, uint8_t slotKind);

// One piece in one of an owner's slots, without the display name (cheap to read).
struct SlotPiece {
    uint8_t slot = 0;
    uint64_t handle = 0;
    uint32_t type = 0;
};

// What an owner holds in its own slots of the given kinds (0 = the local player, a vehicle's id, a remote body's network
// id), in one pass over the pool; empty when there is no such owner. Any thread.
std::vector<SlotPiece> slotPiecesOfKinds(uint64_t ownerKey, const uint8_t* kinds, size_t count);

// Every piece an owner other than the local player's holds, in its own slots and in those of its child owners (the
// backpack). Empty for the local player's tree.
std::vector<Cargo> ownedCargo(uint64_t ownerKey);

// The remote body's pieces are baggage in the world's pool, so a save writes them, and the next load drops them on the
// ground as orphans. Each piece the body holds carries a mark in its durability (it survives the save); a loose piece with
// the mark is one of the body's, whichever machine loaded the save. Any thread.
void markOwnedCargo(uint64_t ownerKey);
std::vector<uint64_t> markedLooseCargo();

// Asks the game to create a piece of `type` in that owner's slot of `slotKind`. False when there is no such owner or
// the game refused. Any thread.
bool addSlotPiece(uint64_t ownerKey, uint8_t slotKind, uint32_t type);

// Asks the game to put `piece` on the ground at `at`, as the world's own cargo is spawned: it starts a little above the
// spot and falls onto the ground. An order piece keeps its link: a copy of it this world holds elsewhere is removed first
// (Retry), and one the local backpack holds is refused. Refused too when the game refused (e.g. its pool is full). Any
// thread.
AddResult placeCargo(const Cargo& piece, const world_to_screen::Vec3& at);

// Asks the game to delete a piece, carried or on the ground. Any thread.
bool removeCargo(uint64_t handle);

// Queues the deletion for the simulation thread, a few per frame (the partner body's pieces: a burst from the net thread
// crashed the engine's equipment code). Any thread.
void removeCargoLater(uint64_t handle);

// While `on` (the host), the story, order and progress facts the game changes during gameplay are queued, last value
// per fact; loading and the title screen are never queued. Any thread. Needs world_facts::installEarly.
void shareFactWrites(bool on);

// The queued fact changes, oldest first; empties the queue. Any thread.
std::vector<fact_wire::Entry> takeFactWrites();

// Every fact the game changed during the host's gameplay since sharing started (the last value of each), for a guest
// that needs the whole picture; the queue above only holds what is new. Any thread.
std::vector<fact_wire::Entry> factSnapshot();

// Whether the local player has been in gameplay long enough for the world to have settled (not loading, not on the
// title screen); the same gate the fact queue uses. Any thread.
bool gameplaySettled();

// Writes one fact into this world's fact database as the game's own writer does (the guest following the host).
// False until the game has written a fact itself, which is how the database is found. Any thread.
bool applyFact(const fact_wire::Entry& fact);

// The world's time of day and weather as the game holds them. False until the world exists. Any thread.
bool readWorldEnv(env_wire::WorldEnv& out);

// Guest: from now on the world's clock and weather follow `env` (applied on the game's next time and weather updates:
// the time snaps when it drifted, region weather is set through the game's own setter, the guest's forecast is
// pinned). Call again with each newer WORLD_ENV. Any thread.
void followWorldEnv(const env_wire::WorldEnv& env);

// Stops following: the world runs its own clock and forecast again. Any thread.
void releaseWorldEnv();

// While linked the whole world keeps running under the game's own pauses (menus, weapon wheel, pause menu). Any thread.
void keepWorldClockRunning(bool linked);
void keepWorldRunning(bool linked);

// Guest: while `tame` is true the enemies the game spawns (BTs, MULEs, armed humans, catchers, hunters) are put to sleep
// as they are built, and the host's reports (enemy_wire.h) move them: they are the puppets of the host's enemies. Needs
// enemy_spawn::installEarly (adapter.ini enemy_sync=1). Any thread.
void tameEnemies(bool tame);

// Host: report the alert phase of the enemy camps. `takeCampPhases` returns the camps whose phase changed since the last
// call, or all of them when `all`. A guest sets the received phases on its own camps. Any thread.
void shareCamps(bool host);
std::vector<camp_wire::CampPhase> takeCampPhases(bool all);
void applyCampPhases(const std::vector<camp_wire::CampPhase>& camps);

// Host: report the enemies the game spawns (needs the enemy hook, adapter.ini enemy_sync=1). Any thread.
void shareEnemies(bool host);

// Host: the next report announces every live enemy again (a joined or resyncing guest). Any thread.
void requestEnemySnapshot();

// Host: what to send since the last call. Any thread.
std::vector<enemy_wire::EnemySpawn> takeEnemySpawns();
std::vector<enemy_wire::EnemyState> takeEnemyStates();
std::vector<enemy_wire::EnemyGone> takeEnemyGone();
std::vector<enemy_wire::EnemyAnim> takeEnemyAnims();

// Guest: the host's reports, applied on the simulation thread (puppets are built from the vetoed spawn requests).
// Any thread.
void puppetSpawn(const enemy_wire::EnemySpawn& spawn);
void puppetStates(const std::vector<enemy_wire::EnemyState>& states);
void puppetGone(const enemy_wire::EnemyGone& gone);
void puppetAnim(enemy_wire::EnemyAnim anim);

// Enemy combat (combat_wire.h; needs combat_hook::installEarly, adapter.ini enemy_sync=1). The role decides what the
// engine's damage function does: a guest sends damage to puppets instead of applying it, the host sends damage to the
// partner's body to that partner. Any thread.
enum class CombatRole : uint8_t { None, Host, Guest };
void setCombatRole(CombatRole role);

// Guest: the hits on puppets that were not applied here since the last call.
std::vector<combat_wire::EnemyHit> takeEnemyHits();

// Host: the hits on the partner's body since the last call, with the slot of the partner they belong to.
struct PlayerHitOut {
    uint8_t slot;
    combat_wire::PlayerHit hit;
};
std::vector<PlayerHitOut> takePlayerHits();

// Host: the enemies of the directory (enemy_directory.h) that died since the last call.

// Host: whether the engine has this enemy and it is alive. A guest's hit is only accepted for such an enemy.
bool enemyAlive(const uint8_t (&uuid)[enemy_wire::kUuidSize]);

// Applied on the simulation thread's next frame, through the engine's damage function with the applying flag set.
void applyEnemyHit(const combat_wire::EnemyHit& hit);    // host: the guest's hit on its enemy
void applyPlayerHit(const combat_wire::PlayerHit& hit);  // guest: an enemy's hit on the local player

// The structure roles: the host reports what its player places and removes, a guest refuses its own player's
// placements. Needs structures::installEarly. Any thread.
void setStructureRole(bool host, bool guest);

// Host: the structures its player placed and removed since the last call (ladders so far). Any thread.
std::vector<struct_wire::Placed> takePlacedStructures();
std::vector<struct_wire::Remove> takeRemovedStructures();

// Guest: builds the host's structure under the host's id / removes it, on the simulation thread's next frame. Any thread.
void buildStructure(const struct_wire::Placed& placed);
void removeStructure(const struct_wire::Remove& removal);

// The story roles: the host reports its missions and story sections, a guest vetoes its own story requests and replays
// the host's. Needs story::installEarly. Any thread.
void setStoryRole(bool host, bool guest);

// Host: the next poll also reports every mission in progress (a joined or resynced guest). Any thread.
void requestStorySnapshot();

// Guest: the order starts the player asked for at a terminal (refused locally, the host runs them). Any thread.
std::vector<story_wire::Event> takeStoryRequests();

// Host: the story events since the last call. Any thread.
std::vector<story_wire::Event> takeStoryEvents();

// Guest: replays the host's event on the simulation thread's next frame. Any thread.
void replayStoryEvent(const story_wire::Event& event);

// The weapon the local player has drawn (the engine's weapon table of his entity), or nothing until he exists. Needs
// local_weapon::installEarly for shots. Any thread.
std::optional<weapon_wire::WeaponState> localWeaponState();

// The shots and throws the local player made since the last call, oldest first. Any thread.
std::vector<weapon_wire::WeaponFire> takeLocalFires();

// The weapon the partner's body should hold (kHolstered: none), made on the simulation thread's next frame; and one of
// its shots, played by the body's weapon on that frame. Needs remote_weapon::installEarly. Any thread.
void setPartnerWeapon(const weapon_wire::WeaponState& state);
void partnerFire(const weapon_wire::WeaponFire& fire);

}  // namespace game
