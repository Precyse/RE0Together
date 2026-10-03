#pragma once
// The engine-specific layer: everything the co-op code needs from the running game. One implementation per game
// build (src/ds2/game.cpp for DEATH STRANDING 2); the rest of the adapter only talks to this interface.
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "env_wire.h"
#include "fact_wire.h"
#include "struct_wire.h"
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

// What the local player's backpack holds: cargo, and weapons and tools stowed in it, but not the equipped gear.
// Any thread.
std::vector<Cargo> carriedCargo();

// A piece lying loose in the world (on no one's rack, in no locker or vehicle).
struct LooseCargo {
    uint64_t handle = 0;
    uint32_t type = 0;
    world_to_screen::Vec3 position;
};

// Loose pieces within `radius` metres of `around`. Any thread.
std::vector<LooseCargo> looseCargo(const world_to_screen::Vec3& around, double radius);

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

// What one slot kind of the baggage owner `ownerKey` holds (0 = the local player, a vehicle's id, a remote body's
// network id); empty when there is no such owner. Any thread.
std::vector<Cargo> slotPieces(uint64_t ownerKey, uint8_t slotKind);

// Asks the game to create a piece of `type` in that owner's slot of `slotKind`. False when there is no such owner or
// the game refused. Any thread.
bool addSlotPiece(uint64_t ownerKey, uint8_t slotKind, uint32_t type);

// Asks the game to put a piece of `type` on the ground at `at`, as the world's own cargo is spawned: it starts a
// little above the spot and falls onto the ground. False when the game refused (e.g. its pool is full). Any thread.
bool placeCargo(uint32_t type, const world_to_screen::Vec3& at);

// Asks the game to delete a piece, carried or on the ground. Any thread.
bool removeCargo(uint64_t handle);

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

// Guest: while `veto` is true the game's own spawns of enemies (BTs, MULEs, armed humans, catchers, hunters) fail, so
// the host's enemies are the only ones (they appear as puppets). Needs enemy_veto::installEarly (adapter.ini
// enemy_veto=1). Any thread.
void vetoEnemies(bool veto);

// The structure roles: the host reports what its player places and removes, a guest refuses its own player's
// placements. Needs structures::installEarly. Any thread.
void setStructureRole(bool host, bool guest);

// Host: the structures its player placed and removed since the last call (ladders so far). Any thread.
std::vector<struct_wire::Placed> takePlacedStructures();
std::vector<struct_wire::Remove> takeRemovedStructures();

// Guest: builds the host's structure under the host's id / removes it, on the simulation thread's next frame. Any thread.
void buildStructure(const struct_wire::Placed& placed);
void removeStructure(const struct_wire::Remove& removal);

}  // namespace game
