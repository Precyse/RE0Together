// DEATH STRANDING 2: the partner's weapon on its body. The engine fills a player's weapon table through
// 0x140eab2f0(entry, weaponId): it looks the weapon up, creates the entity, activates it, makes it owned by and
// attached to the table's player (SetOwner, which parents it), sends the shadow flag message and enables its
// components. Here that call runs on a free entry of the body's own weapon table and the table's current index is set
// to it, so the table's own update draws and attaches the weapon to the hand as it does for Sam's. The weapon is removed
// through the entry's reset (0x140eab0f0), which asks the engine to remove the entity, and the index goes back to 0. A
// shot sets the fire request byte of the weapon's behavior.
#include "ds2/remote_weapon.h"

#include <windows.h>

#include <array>
#include <mutex>
#include <vector>

#include "decima/safe_read.h"
#include "decima/world_transform.h"
#include "ds2/engine.h"
#include "ds2/entity_lookup.h"
#include "ds2/place.h"
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
constexpr ULONGLONG kProbeDelayMs = 1000;          // after the weapon is made: where it is and whether it is still there
constexpr size_t kMaxQueuedFires = 64;
constexpr uintptr_t kEntityParent = 0x80, kEntityFlags = 0x98;

using CreateFn = void (*)(uintptr_t entry, uint16_t weaponId);
using ResetFn = void (*)(uintptr_t entry);
using SetParentFn = void (*)(uintptr_t entity, uintptr_t parent, uint32_t mode);
using Uuid = std::array<uint8_t, 16>;

uint8_t g_attachMode = kEngineAttachMode;

std::mutex g_mutex;  // the net thread hands over, the simulation thread takes
weapon_wire::WeaponState g_wanted{weapon_wire::kHolstered, 0, 0};
std::vector<weapon_wire::WeaponFire> g_fires;

struct Held {
    uintptr_t owner = 0;   // the body the weapon was made for
    uint16_t id = weapon_wire::kHolstered;  // the weapon last asked for on this body (made or not)
    uintptr_t weapon = 0;  // the entity we made; checked by UUID before every use
    uintptr_t table = 0;   // the body's weapon table component and the entry of it the weapon lives in
    uintptr_t entry = 0;
    Uuid uuid{};
    ULONGLONG retryAt = 0;
    ULONGLONG probeAt = 0;  // when the weapon's state is logged a second time, 0 when it is not pending
    bool shotLogged = false;
};
Held g_held;

bool weaponAlive() { return g_held.weapon && ds2::entityExists(g_held.uuid.data()); }

// Where the weapon is against the body's hand, who it hangs from and its entity flags (+0x98), to tell a hidden or
// misplaced weapon from a removed one.
void logWeaponState(const char* when, uintptr_t body) {
    decima::WorldTransform weaponAt{}, bodyAt{};
    uint64_t flags = 0;
    const bool placed = ds2::entityTransform(g_held.weapon, weaponAt) && ds2::entityTransform(body, bodyAt);
    decima::safeRead(g_held.weapon + kEntityFlags, flags);
    logger::write("remote_weapon: %s: weapon %u alive %d, parent %p (body %p), flags %llx, weapon at (%.2f, %.2f, %.2f), body at (%.2f, %.2f, %.2f)",
                  when, g_held.id, weaponAlive(), reinterpret_cast<void*>(decima::readPointer(g_held.weapon + kEntityParent)),
                  reinterpret_cast<void*>(body), static_cast<unsigned long long>(flags), placed ? weaponAt.position.x : 0.0,
                  placed ? weaponAt.position.y : 0.0, placed ? weaponAt.position.z : 0.0, placed ? bodyAt.position.x : 0.0,
                  placed ? bodyAt.position.y : 0.0, placed ? bodyAt.position.z : 0.0);
}

// The body's table is the real one, so its own update draws, holsters and attaches the weapon like Sam's: the weapon is
// made in a free entry of that table and the table's current index is set to it. A free entry has no weapon and keeps
// the table component the engine put in it (+0x38).
uintptr_t freeEntry(uintptr_t table) {
    for (uint32_t i = 1; i < ds2::weapon::kTableEntries; ++i) {
        const uintptr_t entry = table + ds2::weapon::kTableFirstEntry + i * ds2::weapon::kEntrySize;
        if (!decima::readPointer(entry + ds2::weapon::kEntryWeapon) && decima::readPointer(entry + ds2::weapon::kEntryTable) == table) {
            return entry;
        }
    }
    return 0;
}

uint32_t indexOf(uintptr_t table, uintptr_t entry) {
    return static_cast<uint32_t>((entry - table - ds2::weapon::kTableFirstEntry) / ds2::weapon::kEntrySize);
}

// Engine calls: no C++ objects with destructors in functions that hold __try.
uintptr_t createGuarded(uintptr_t entry, uint16_t id) {
    __try {
        reinterpret_cast<CreateFn>(ds2::at(kCreateInTable))(entry, id);
        return ds2::field<uintptr_t>(entry, ds2::weapon::kEntryWeapon);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

bool removeGuarded(uintptr_t entry) {
    __try {
        reinterpret_cast<ResetFn>(ds2::at(kResetEntry))(entry);
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

// Gives the weapon's entry back to the table: holstered (index 0), weapon removed, entry free again. Only for a body
// that still exists; `body`'s table memory is read and written here.
void dropWeapon(uintptr_t body) {
    if (g_held.entry && decima::readPointer(body + ds2::weapon::kEntityTable) == g_held.table) {
        uint32_t current = 0;
        if (decima::safeRead(g_held.table + ds2::weapon::kTableCurrentIndex, current) && current == indexOf(g_held.table, g_held.entry)) {
            ds2::field<uint32_t>(g_held.table, ds2::weapon::kTableCurrentIndex) = 0;
        }
        if (weaponAlive()) {
            if (removeGuarded(g_held.entry)) {
                logger::write("remote_weapon: removed weapon %u from the body's table entry %u", g_held.id, indexOf(g_held.table, g_held.entry));
            } else {
                logger::write("remote_weapon: removing the weapon faulted");
            }
        }
        ds2::field<uintptr_t>(g_held.entry, ds2::weapon::kEntryWeapon) = 0;
    }
    g_held.weapon = 0;
    g_held.entry = 0;
}

void createWeapon(uintptr_t body, uint16_t id) {
    const uintptr_t table = decima::readPointer(body + ds2::weapon::kEntityTable);
    if (!table || decima::readPointer(table + ds2::weapon::kTableOwner) != body) {
        logger::write("remote_weapon: the body has no weapon table of its own, no weapon %u made", id);
        return;
    }
    const uintptr_t entry = freeEntry(table);
    if (!entry) {
        logger::write("remote_weapon: the body's weapon table has no free entry, no weapon %u made", id);
        return;
    }
    const uintptr_t weapon = createGuarded(entry, id);
    if (!weapon || !decima::safeCopy(g_held.uuid.data(), weapon + ds2::kEntityUuid, g_held.uuid.size())) {
        logger::write("remote_weapon: the engine made no weapon %u for the body", id);
        return;
    }
    g_held.weapon = weapon;
    g_held.table = table;
    g_held.entry = entry;
    g_held.shotLogged = false;
    if (g_attachMode != kEngineAttachMode && !reattachGuarded(weapon, body, g_attachMode)) {
        logger::write("remote_weapon: attaching with mode %u faulted", g_attachMode);
    }
    ds2::field<uint32_t>(table, ds2::weapon::kTableCurrentIndex) = indexOf(table, entry);
    logger::write("remote_weapon: the body holds weapon %u in table entry %u (attach mode %u)", id, indexOf(table, entry), g_attachMode);
    logWeaponState("made", body);
    g_held.probeAt = GetTickCount64() + kProbeDelayMs;
}

// Brings the body's weapon to the one the partner has drawn.
void follow(uintptr_t body, const weapon_wire::WeaponState& wanted) {
    if (g_held.owner != body) {
        g_held = {};  // another body: the old one's table is gone with it
        g_held.owner = body;
    }
    const ULONGLONG now = GetTickCount64();
    if (g_held.weapon && !weaponAlive()) {
        logger::write("remote_weapon: the engine removed the body's weapon %u", g_held.id);
        dropWeapon(body);
        g_held.id = weapon_wire::kHolstered;
        g_held.retryAt = now + kRetryDelayMs;
    }
    if (g_held.probeAt && now >= g_held.probeAt) {
        g_held.probeAt = 0;
        if (g_held.weapon) logWeaponState("after 1 s", body);
    }
    if (g_held.id == wanted.weaponId || now < g_held.retryAt) return;
    dropWeapon(body);
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
