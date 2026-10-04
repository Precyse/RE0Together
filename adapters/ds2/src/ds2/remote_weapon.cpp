// DEATH STRANDING 2: the partner's weapon on its body. The engine fills a player's weapon table through
// 0x140eab2f0(entry, weaponId): it looks the weapon up, creates the entity, activates it, makes it owned by and
// attached to the table's player (SetOwner, which parents it so it follows the hand joint), sends the shadow flag
// message and enables its components. Here that call runs on a table entry of our own (a zeroed 0x40-byte scratch whose
// table field is the body's table component), so the body gets exactly the engine's weapon and Sam's table is never
// read or written. The weapon is removed through the same entry's reset (0x140eab0f0) with no table, which asks the
// engine to remove the entity. A shot sets the fire request byte of the weapon's behavior.
#include "ds2/remote_weapon.h"

#include <windows.h>

#include <array>
#include <cstring>
#include <mutex>
#include <vector>

#include "decima/safe_read.h"
#include "ds2/engine.h"
#include "ds2/entity_lookup.h"
#include "ds2/remote_player.h"
#include "ds2/sim_tick.h"
#include "ds2/weapon_layout.h"
#include "game.h"
#include "log.h"

namespace {

constexpr uintptr_t kCreateInTable = 0x140eab2f0;  // (entry, u16 weapon id): creates, owns and attaches the weapon
constexpr uintptr_t kResetEntry = 0x140eab0f0;     // (entry): with no table, asks the engine to remove the entry's weapon
constexpr uintptr_t kSetParent = 0x140130900;      // (entity, parent, mode)
constexpr uint32_t kEngineAttachMode = 1;          // the mode the table creation attaches with
constexpr ULONGLONG kRetryDelayMs = 1000;          // before a weapon the engine took away is made again
constexpr size_t kMaxQueuedFires = 64;

using CreateFn = void (*)(uintptr_t entry, uint16_t weaponId);
using ResetFn = void (*)(uintptr_t entry);
using SetParentFn = void (*)(uintptr_t entity, uintptr_t parent, uint32_t mode);
using Uuid = std::array<uint8_t, 16>;

uint8_t g_attachMode = kEngineAttachMode;

std::mutex g_mutex;  // the net thread hands over, the simulation thread takes
weapon_wire::WeaponState g_wanted{weapon_wire::kHolstered, 0, 0};
std::vector<weapon_wire::WeaponFire> g_fires;

// Simulation thread only.
alignas(16) uint8_t g_entry[ds2::weapon::kEntrySize];

struct Held {
    uintptr_t owner = 0;   // the body the weapon was made for
    uint16_t id = weapon_wire::kHolstered;  // the weapon last asked for on this body (made or not)
    uintptr_t weapon = 0;  // the entity we made; checked by UUID before every use
    Uuid uuid{};
    ULONGLONG retryAt = 0;
    bool shotLogged = false;
};
Held g_held;

bool weaponAlive() { return g_held.weapon && ds2::entityExists(g_held.uuid.data()); }

// Engine calls: no C++ objects with destructors in functions that hold __try.
uintptr_t createGuarded(uintptr_t table, uint16_t id) {
    __try {
        std::memset(g_entry, 0, sizeof(g_entry));
        ds2::field<uintptr_t>(reinterpret_cast<uintptr_t>(g_entry), ds2::weapon::kEntryTable) = table;
        reinterpret_cast<CreateFn>(ds2::at(kCreateInTable))(reinterpret_cast<uintptr_t>(g_entry), id);
        return ds2::field<uintptr_t>(reinterpret_cast<uintptr_t>(g_entry), ds2::weapon::kEntryWeapon);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

bool removeGuarded(uintptr_t weapon) {
    __try {
        std::memset(g_entry, 0, sizeof(g_entry));
        ds2::field<uintptr_t>(reinterpret_cast<uintptr_t>(g_entry), ds2::weapon::kEntryWeapon) = weapon;
        reinterpret_cast<ResetFn>(ds2::at(kResetEntry))(reinterpret_cast<uintptr_t>(g_entry));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool reattachGuarded(uintptr_t weapon, uintptr_t owner, uint32_t mode) {
    __try {
        reinterpret_cast<SetParentFn>(ds2::at(kSetParent))(weapon, owner, mode);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void dropWeapon() {
    if (weaponAlive() && !removeGuarded(g_held.weapon)) logger::write("remote_weapon: removing the weapon faulted");
    g_held.weapon = 0;
}

void createWeapon(uintptr_t body, uint16_t id) {
    const uintptr_t table = decima::readPointer(body + ds2::weapon::kEntityTable);
    if (!table || decima::readPointer(table + ds2::weapon::kTableOwner) != body) {
        logger::write("remote_weapon: the body has no weapon table of its own, no weapon %u made", id);
        return;
    }
    const uintptr_t weapon = createGuarded(table, id);
    if (!weapon || !decima::safeCopy(g_held.uuid.data(), weapon + ds2::kEntityUuid, g_held.uuid.size())) {
        logger::write("remote_weapon: the engine made no weapon %u for the body", id);
        return;
    }
    g_held.weapon = weapon;
    g_held.shotLogged = false;
    if (g_attachMode != kEngineAttachMode && !reattachGuarded(weapon, body, g_attachMode)) {
        logger::write("remote_weapon: attaching with mode %u faulted", g_attachMode);
    }
    logger::write("remote_weapon: the body holds weapon %u (attach mode %u)", id, g_attachMode);
}

// Brings the body's weapon to the one the partner has drawn.
void follow(uintptr_t body, const weapon_wire::WeaponState& wanted) {
    if (g_held.owner != body) {
        dropWeapon();
        g_held = {};
        g_held.owner = body;
    }
    const ULONGLONG now = GetTickCount64();
    if (g_held.weapon && !weaponAlive()) {
        logger::write("remote_weapon: the engine removed the body's weapon %u", g_held.id);
        g_held.weapon = 0;
        g_held.id = weapon_wire::kHolstered;
        g_held.retryAt = now + kRetryDelayMs;
    }
    if (g_held.id == wanted.weaponId || now < g_held.retryAt) return;
    dropWeapon();
    g_held.id = wanted.weaponId;
    if (wanted.weaponId != weapon_wire::kHolstered) createWeapon(body, wanted.weaponId);
}

void playShot(const weapon_wire::WeaponFire& fire) {
    if (!weaponAlive() || fire.weaponId != g_held.id) return;
    const uintptr_t behavior = ds2::weapon::shotBehavior(g_held.weapon);
    if (!behavior) return;
    ds2::field<uint8_t>(behavior, ds2::weapon::kBehaviorFireRequest) = 1;
    if (g_held.shotLogged) return;
    g_held.shotLogged = true;
    logger::write("remote_weapon: first shot of weapon %u: origin (%.2f, %.2f, %.2f) direction (%.2f, %.2f, %.2f) pellets %u",
                  fire.weaponId, fire.origin[0], fire.origin[1], fire.origin[2], fire.direction[0], fire.direction[1],
                  fire.direction[2], fire.pellets);
}

// Simulation thread, ahead of the engine's object update.
void tick() {
    weapon_wire::WeaponState wanted;
    std::vector<weapon_wire::WeaponFire> fires;
    {
        std::lock_guard lock(g_mutex);
        wanted = g_wanted;
        fires.swap(g_fires);
    }
    const uintptr_t body = remote_player::isLive() ? remote_player::entity() : 0;
    if (!body) {
        dropWeapon();
        g_held = {};
        return;
    }
    follow(body, wanted);
    for (const weapon_wire::WeaponFire& fire : fires) playShot(fire);
}

}  // namespace

namespace remote_weapon {

void installEarly(uint8_t attachMode) {
    g_attachMode = attachMode;
    sim_tick::add(&tick, "remote weapon");
}

}  // namespace remote_weapon

namespace game {

void setPartnerWeapon(const weapon_wire::WeaponState& state) {
    std::lock_guard lock(g_mutex);
    g_wanted = state;
}

void partnerFire(const weapon_wire::WeaponFire& fire) {
    std::lock_guard lock(g_mutex);
    if (g_fires.size() < kMaxQueuedFires) g_fires.push_back(fire);
}

}  // namespace game
