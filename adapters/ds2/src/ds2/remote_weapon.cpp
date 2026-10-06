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
#include <atomic>
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
#include "hooks.h"
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
constexpr uintptr_t kWeaponUpdateGate = 0x142000790;  // DSWeaponBehaviorComponent slot 41(behavior, dt): runs the shot when +0x4D1 is set
constexpr uintptr_t kWeaponEnabled = 0x363;     // byte: the weapon entity's update (0x141fa9850, 0x141fa9760) runs its behaviors only when set
constexpr uintptr_t kWeaponBehaviorsReady = 0x21E0;  // byte: set by the entity's slot 38 (0x141fa9f60), which prepares its behaviors
constexpr uintptr_t kWeaponActive = 0x24B9;     // byte: set by the player's state when it equips the weapon (0x141f57f30); the behavior's can't-fire check (slot 60, 0x14202a500) refuses a weapon with it 0
constexpr uintptr_t kPrepareBehaviors = 0x141faa350;  // (weapon): runs vtable slot 68 of every behavior, the last step of the player's equip
constexpr size_t kWeaponEnableSlot = 28, kWeaponPrepareSlot = 38;  // DSWeaponEntity vtable slots: enable (0x141fa99e0), prepare behaviors
constexpr double kAimDistanceMetres = 100.0;  // how far along the partner's shot direction the body's aim target is put
constexpr ULONGLONG kFireCheckDelayMs = 300;  // after a fire request: what the weapon's update made of it
constexpr ULONGLONG kShotLogWarmupMs = 4000;  // a shot sooner after the weapon is made is not the one logged (its behavior is not set up yet)
constexpr uintptr_t kBulletSystemGlobal = 0x14623fa48;  // the bullet pool (0x141fb5c70 adds a bullet per pellet)
constexpr uintptr_t kBulletsMade = 0x299f28;            // u32: bullets created so far

using CreateFn = void (*)(uintptr_t entry, uint16_t weaponId);
using ResetFn = void (*)(uintptr_t entry);
using SetParentFn = void (*)(uintptr_t entity, uintptr_t parent, uint32_t mode);
using Uuid = std::array<uint8_t, 16>;

uint8_t g_attachMode = kEngineAttachMode;
bool g_diagnostics = false;

std::mutex g_mutex;  // the net thread hands over, the simulation thread takes
weapon_wire::WeaponState g_wanted{weapon_wire::kHolstered, 0, 0};
std::vector<weapon_wire::WeaponFire> g_fires;
std::atomic<uintptr_t> g_madeWeapon{0};  // the weapon entity made for the body, for the shot detours on other threads
std::atomic<uint32_t> g_engineShots{0};
std::atomic<bool> g_shotPending{false};  // a partner's shot waits for the body weapon's next update gate
std::atomic<uint32_t> g_gateRuns{0};  // the engine's update gate called for the weapon made for the body
void (*g_originalGate)(uintptr_t behavior, float dt) = nullptr;

// The partner's shot: the behavior's own state machine would count the pellets, but it runs the player's fire callback
// (0x140ea5f00), which reads the owner's current-weapon reference the body does not have (a null read crashed the game). So
// the shot is handed to the gate directly: one pellet with no spread and the fire request, which the gate turns into the
// shot (slots 44, 45, 46) and clears.
void gateDetour(uintptr_t behavior, float dt) {
    const uintptr_t made = g_madeWeapon.load();
    if (made && decima::readPointer(behavior + ds2::weapon::kBehaviorWeapon) == made) {
        ++g_gateRuns;
        if (behavior == ds2::weapon::shotBehavior(made) && g_shotPending.exchange(false)) {
            ds2::field<uint32_t>(behavior, ds2::weapon::kBehaviorPellets) = 1;
            ds2::field<float>(behavior, ds2::weapon::kBehaviorPelletSpread) = 0.0f;
            ds2::field<uint8_t>(behavior, ds2::weapon::kBehaviorFireRequest) = 1;
        }
    }
    g_originalGate(behavior, dt);
}

uint32_t bulletsMade() {
    const uintptr_t pool = decima::readPointer(ds2::at(kBulletSystemGlobal));
    uint32_t made = 0;
    return pool && decima::safeRead(pool + kBulletsMade, made) ? made : 0;
}

struct Held {
    uintptr_t owner = 0;   // the body the weapon was made for
    uint16_t id = weapon_wire::kHolstered;  // the weapon last asked for on this body (made or not)
    uintptr_t weapon = 0;  // the entity we made; checked by UUID before every use
    uintptr_t table = 0;   // the body's weapon table component and the entry of it the weapon lives in
    uintptr_t entry = 0;
    Uuid uuid{};
    ULONGLONG retryAt = 0;
    ULONGLONG madeAt = 0;
    ULONGLONG probeAt = 0;  // when the weapon's state is logged a second time, 0 when it is not pending
    ULONGLONG fireCheckAt = 0;  // when the first fire request is looked at again, 0 when none is pending
    uint32_t bulletsAtFire = 0;
    bool shotLogged = false;
};
Held g_held;

bool weaponAlive() { return g_held.weapon && ds2::entityExists(g_held.uuid.data()); }

uint32_t indexOf(uintptr_t table, uintptr_t entry) {
    return static_cast<uint32_t>((entry - table - ds2::weapon::kTableFirstEntry) / ds2::weapon::kEntrySize);
}

// The weapon's entity flags (+0x98), its parent and the table's two indices, to tell a holstered weapon (flag bit 0x2 clear)
// from a drawn one (the weapon's transform field is not its world position while it is attached, so none is logged).
void logWeaponState(const char* when, uintptr_t body) {
    uint8_t enabled = 0, ready = 0;
    decima::safeRead(g_held.weapon + kWeaponEnabled, enabled);
    decima::safeRead(g_held.weapon + kWeaponBehaviorsReady, ready);
    logger::write("remote_weapon: %s: weapon enabled byte %u, behaviors ready byte %u", when, enabled, ready);
    uint64_t flags = 0;
    uint32_t current = 0, requested = 0;
    decima::safeRead(g_held.weapon + kEntityFlags, flags);
    decima::safeRead(g_held.table + ds2::weapon::kTableCurrentIndex, current);
    decima::safeRead(g_held.table + ds2::weapon::kTableRequestedIndex, requested);
    logger::write("remote_weapon: %s: weapon %u alive %d, table index %u / requested %u (entry %u), parent %p (body %p), flags %llx",
                  when, g_held.id, weaponAlive(), current, requested, indexOf(g_held.table, g_held.entry),
                  reinterpret_cast<void*>(decima::readPointer(g_held.weapon + kEntityParent)), reinterpret_cast<void*>(body),
                  static_cast<unsigned long long>(flags));
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

// Engine calls: no C++ objects with destructors in functions that hold __try.
uintptr_t createGuarded(uintptr_t entry, uint16_t id) {
    __try {
        reinterpret_cast<CreateFn>(ds2::at(kCreateInTable))(entry, id);
        return ds2::field<uintptr_t>(entry, ds2::weapon::kEntryWeapon);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// Runs one of the weapon entity's virtual functions (slot, this). False when it faulted.
bool callWeaponSlot(uintptr_t weapon, size_t slot) {
    __try {
        const uintptr_t table = ds2::field<uintptr_t>(weapon, 0);
        reinterpret_cast<void (*)(uintptr_t)>(ds2::field<uintptr_t>(table, slot * sizeof(uintptr_t)))(weapon);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool prepareBehaviorsGuarded(uintptr_t weapon) {
    __try {
        reinterpret_cast<void (*)(uintptr_t)>(ds2::at(kPrepareBehaviors))(weapon);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The table enables a weapon it draws and the player's state equips it (active byte, prepare slot, behaviors' own prepare);
// the weapon made for the body gets the same steps, so its entity update runs its behaviors and they may fire.
void enableWeapon(uintptr_t weapon) {
    if (!ds2::field<uint8_t>(weapon, kWeaponEnabled) && !callWeaponSlot(weapon, kWeaponEnableSlot)) {
        logger::write("remote_weapon: enabling the weapon faulted");
    }
    ds2::field<uint8_t>(weapon, kWeaponActive) = 1;
    if (!ds2::field<uint8_t>(weapon, kWeaponBehaviorsReady) && !callWeaponSlot(weapon, kWeaponPrepareSlot)) {
        logger::write("remote_weapon: preparing the weapon's behaviors faulted");
    }
    if (!prepareBehaviorsGuarded(weapon)) logger::write("remote_weapon: the behaviors' prepare faulted");
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

// The table draws the entry it is told to move to (+0x18C4) and holsters the one it leaves; Sam's table keeps both
// indices equal while a weapon is drawn, so both are written.
void setTableIndex(uintptr_t table, uint32_t index) {
    ds2::field<uint32_t>(table, ds2::weapon::kTableCurrentIndex) = index;
    ds2::field<uint32_t>(table, ds2::weapon::kTableRequestedIndex) = index;
}

// Diagnostics: the body's weapon against Sam's weapon of the same id (his holstered copy), entity and shot behavior, listing the
// qwords that differ and are not both pointers. A drawn and initialized weapon differs from the body's in the fields that make the
// engine update it.
constexpr size_t kCompareEntityBytes = 0x800, kCompareBehaviorBytes = 0x900, kMaxDifferencesLogged = 40;

bool pointerLike(uint64_t value) { return value > 0x10000 && value < 0x7FFFFFFF0000ull; }

void logDifferences(const char* what, uintptr_t sam, uintptr_t body, size_t bytes) {
    size_t logged = 0;
    for (size_t offset = 0; offset < bytes && logged < kMaxDifferencesLogged; offset += sizeof(uint64_t)) {
        uint64_t a = 0, b = 0;
        if (!decima::safeRead(sam + offset, a) || !decima::safeRead(body + offset, b) || a == b || (pointerLike(a) && pointerLike(b))) continue;
        logger::write("remote_weapon: %s +0x%zx: Sam's %llx, the body's %llx", what, offset, static_cast<unsigned long long>(a),
                      static_cast<unsigned long long>(b));
        ++logged;
    }
}

void compareWithSam() {
    const uintptr_t sam = remote_player::samEntity();
    const uintptr_t table = sam ? decima::readPointer(sam + ds2::weapon::kEntityTable) : 0;
    uintptr_t samWeapon = 0;
    for (uint32_t i = 1; table && i < ds2::weapon::kTableEntries && !samWeapon; ++i) {
        const uintptr_t weapon = decima::readPointer(table + ds2::weapon::kTableFirstEntry + i * ds2::weapon::kEntrySize + ds2::weapon::kEntryWeapon);
        if (weapon && ds2::weapon::weaponId(weapon) == g_held.id) samWeapon = weapon;
    }
    if (!samWeapon) {
        logger::write("remote_weapon: Sam has no weapon %u to compare with", g_held.id);
        return;
    }
    logDifferences("entity", samWeapon, g_held.weapon, kCompareEntityBytes);
    const uintptr_t samBehavior = ds2::weapon::shotBehavior(samWeapon), bodyBehavior = ds2::weapon::shotBehavior(g_held.weapon);
    if (samBehavior && bodyBehavior) logDifferences("behavior", samBehavior, bodyBehavior, kCompareBehaviorBytes);
}

// Gives the weapon's entry back to the table: holstered (index 0), weapon removed, entry free again. Only for a body
// that still exists; `body`'s table memory is read and written here.
void dropWeapon(uintptr_t body) {
    if (g_held.entry && decima::readPointer(body + ds2::weapon::kEntityTable) == g_held.table) {
        uint32_t current = 0;
        if (decima::safeRead(g_held.table + ds2::weapon::kTableCurrentIndex, current) && current == indexOf(g_held.table, g_held.entry)) {
            setTableIndex(g_held.table, 0);
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
    g_madeWeapon = 0;
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
    g_madeWeapon = weapon;
    if (g_attachMode != kEngineAttachMode && !reattachGuarded(weapon, body, g_attachMode)) {
        logger::write("remote_weapon: attaching with mode %u faulted", g_attachMode);
    }
    logWeaponState("before enabling", body);
    enableWeapon(weapon);
    setTableIndex(table, indexOf(table, entry));
    logger::write("remote_weapon: the body holds weapon %u in table entry %u (attach mode %u)", id, indexOf(table, entry), g_attachMode);
    logWeaponState("made", body);
    uint16_t ids[3] = {};
    if (const uintptr_t behavior = ds2::weapon::shotBehavior(weapon)) ds2::weapon::ammoIds(behavior, ids);
    logger::write("remote_weapon: weapon %u ammo ids %x %x %x (the second is the bullet's damage attack type)", id, ids[0], ids[1], ids[2]);
    g_held.madeAt = GetTickCount64();
    g_held.probeAt = g_held.madeAt + kProbeDelayMs;
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
    if (g_held.fireCheckAt && now >= g_held.fireCheckAt) {
        g_held.fireCheckAt = 0;
        const uintptr_t behavior = weaponAlive() ? ds2::weapon::shotBehavior(g_held.weapon) : 0;
        logger::write("remote_weapon: the first shot, %llu ms later: the engine ran %u shots and made %u bullets",
                      static_cast<unsigned long long>(kFireCheckDelayMs), g_engineShots.load(), bulletsMade() - g_held.bulletsAtFire);
        if (behavior) {
            logger::write("remote_weapon: the update gate ran %u times for the body's weapon; fire state %u, request %u, pellets %u, weapon active byte %u",
                          g_gateRuns.load(), ds2::field<uint32_t>(behavior, ds2::weapon::kBehaviorState),
                          ds2::field<uint32_t>(behavior, ds2::weapon::kBehaviorRequest),
                          ds2::field<uint32_t>(behavior, ds2::weapon::kBehaviorPellets),
                          ds2::field<uint8_t>(g_held.weapon, kWeaponActive));
        }
    }
    if (g_held.probeAt && now >= g_held.probeAt) {
        g_held.probeAt = 0;
        if (g_held.weapon) {
            logWeaponState("after 1 s", body);
            if (g_diagnostics) compareWithSam();
        }
    }
    if (g_held.id == wanted.weaponId || now < g_held.retryAt) return;
    dropWeapon(body);
    g_held.id = wanted.weaponId;
    if (wanted.weaponId != weapon_wire::kHolstered) createWeapon(body, wanted.weaponId);
}

// Points the weapon's aim target along the partner's shot, so the shot request aims where the partner did.
void aimAlong(uintptr_t behavior, const weapon_wire::WeaponFire& fire) {
    const uintptr_t aim = decima::readPointer(behavior + ds2::weapon::kBehaviorAimTarget);
    if (!aim) return;
    for (size_t axis = 0; axis < 3; ++axis) {
        ds2::field<double>(aim, ds2::weapon::kAimPosition + axis * sizeof(double)) =
            static_cast<double>(fire.origin[axis]) + static_cast<double>(fire.direction[axis]) * kAimDistanceMetres;
    }
    ds2::field<uint8_t>(aim, ds2::weapon::kAimFlags) |= 1;
}

void playShot(const weapon_wire::WeaponFire& fire) {
    if (!weaponAlive() || fire.weaponId != g_held.id) return;
    const uintptr_t behavior = ds2::weapon::shotBehavior(g_held.weapon);
    if (!behavior) return;
    aimAlong(behavior, fire);
    g_shotPending = true;
    if (g_held.shotLogged || GetTickCount64() - g_held.madeAt < kShotLogWarmupMs) return;
    g_held.shotLogged = true;
    g_held.fireCheckAt = GetTickCount64() + kFireCheckDelayMs;
    g_held.bulletsAtFire = bulletsMade();
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

void noteEngineShot(uintptr_t behavior) {
    const uintptr_t made = g_madeWeapon.load();
    if (!made || decima::readPointer(behavior + ds2::weapon::kBehaviorWeapon) != made) return;
    if (g_engineShots.fetch_add(1) == 0) logger::write("remote_weapon: the engine runs a shot of the body's weapon");
}

uint16_t attackType() {
    const uintptr_t behavior = weaponAlive() ? ds2::weapon::shotBehavior(g_held.weapon) : 0;
    return behavior ? ds2::weapon::bulletAttackType(behavior) : 0;
}

void installEarly(uint8_t attachMode, bool diagnostics) {
    g_attachMode = attachMode;
    g_diagnostics = diagnostics;
    hooks::install("weapon update gate", ds2::at(kWeaponUpdateGate), reinterpret_cast<void*>(&gateDetour),
                   reinterpret_cast<void**>(&g_originalGate));
    sim_tick::add(&tick, "remote weapon", sim_tick::Gate::Gameplay);
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
