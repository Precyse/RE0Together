#include "player_contact.h"

#include <atomic>

#include "character_owner.h"
#include "contact_game.h"
#include "contact_net.h"
#include "contact_rule.h"
#include "crash_dump.h"
#include "debug_stats.h"
#include "enemy_lease.h"
#include "enemy_observer.h"
#include "enemy_observer_capture.h"
#include "enemy_registry.h"
#include "game.h"
#include "game_state.h"
#include "hooks.h"
#include "log.h"
#include "puppet.h"

namespace {

namespace rule = contact_rule;
using character_owner::Character;

using DamageEntryFunction = uint32_t(__fastcall*)(void* self, void* edx, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e);
DamageEntryFunction g_originalDamageEntry = nullptr;

std::atomic<bool> g_installed{false};

constexpr uint32_t kAlMask = 0xff;  // the entry answers in al; the upper bytes of eax are not the answer

// The original entry with the engine's faults caught: 0 when it raised.
uint32_t callOriginal(void* self, uint32_t attacker, uint32_t point, uint32_t kind, uint32_t level) {
    __try {
        return g_originalDamageEntry(self, nullptr, attacker, point, kind, level, attacker) & kAlMask;
    } __except (crash_dump::reportCaught(GetExceptionInformation(), "player damage entry")) {
        return 0;
    }
}

void log(const rule::Contact& contact, const char* enemyName, Character victim, int32_t state, const char* what) {
    logger::write("contact: slot %u (%s) on %s kind %d level %d -> state %d (%s)", contact.slot, enemyName,
                  character_owner::name(victim), contact.kind, contact.level, state, what);
}

// The contact the engine passed, as the router keeps it. False when its numbers are outside the reaction table.
bool contactOf(int slot, Character victim, uint32_t kind, uint32_t level, uintptr_t enemy, uintptr_t pointAddress,
               rule::Contact& out) {
    constexpr uint32_t kMaxLevel = 0xff;
    if (kind >= rule::kReactionKindCount || level > kMaxLevel) return false;
    out = {static_cast<uint8_t>(slot), static_cast<uint8_t>(victim), static_cast<uint8_t>(kind), static_cast<uint8_t>(level), {}};
    return contact_game::relativePoint(enemy, pointAddress, out.point);
}

// An Observer-class enemy that this machine simulates reached a puppet: the contact goes to the victim's machine.
void route(uintptr_t victim, uintptr_t attacker, uint32_t pointAddress, uint32_t kind, uint32_t level) {
    const Character who = character_owner::identify(victim);
    const int slot = enemy_registry::slotOf(attacker);
    enemy_observer_wire::PoseEntry enemy{};
    if (who == Character::Unknown || slot == enemy_registry::kNoSlot || !enemy_observer_capture::entryFor(slot, attacker, enemy) ||
        !enemy_observer::simulatorOf(static_cast<uint8_t>(slot)).here) {
        return;
    }
    rule::Contact contact;
    int32_t state = 0;
    if (!contactOf(slot, who, kind, level, attacker, pointAddress, contact) || !contact_game::entryState(kind, level, state)) return;
    const rule::Kind classified = rule::classify(state);
    const rule::Situation situation{classified, rule::leaseEligible(enemy.hp, game_state::inCurrentRoom(victim)),
                                    enemy_lease::busy(contact.slot), enemy_lease::busyWith(contact.slot, contact.victim)};
    const char* enemyName = enemy_policy::nameOf(enemy.classIndex);
    const char* kindName = classified == rule::Kind::Paired ? "paired" : "plain";
    switch (rule::route(situation)) {
        case rule::Route::Drop: return;
        case rule::Route::Hit: contact_net::sendHit(contact, enemy); break;
        case rule::Route::Lease: enemy_lease::request(contact, attacker, state); break;
    }
    log(contact, enemyName, who, state, kindName);
}

// The engine's entry refuses while a script pauses the character; a puppet is paused only during its own move, so
// enemies updating outside it are refused here, and the contact goes to the machine that owns the character.
uint32_t __fastcall damageEntryDetour(void* self, void* edx, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    const uintptr_t victim = reinterpret_cast<uintptr_t>(self);
    if (!puppet::active(victim)) return g_originalDamageEntry(self, edx, a, b, c, d, e);
    route(victim, a, b, c, d);
    return 0;
}

}  // namespace

namespace player_contact {

bool enable() {
    g_installed = hooks::install("player damage entry", game::kPlayerDamageEntryFunction,
                                 reinterpret_cast<void*>(&damageEntryDetour), reinterpret_cast<void**>(&g_originalDamageEntry));
    return g_installed;
}

void uninstall() {
    if (g_installed.exchange(false)) hooks::remove(game::kPlayerDamageEntryFunction);
}

bool applyToLocal(uintptr_t victim, uintptr_t enemy, const float (&point)[3], uint32_t kind, uint32_t level) {
    if (!g_installed) return false;
    float world[3] = {point[0], point[1], point[2]};
    return callOriginal(reinterpret_cast<void*>(victim), static_cast<uint32_t>(enemy), static_cast<uint32_t>(reinterpret_cast<uintptr_t>(world)),
                        kind, level) != 0;
}

}  // namespace player_contact
