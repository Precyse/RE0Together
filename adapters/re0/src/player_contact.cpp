#include "player_contact.h"

#include <atomic>

#include "game.h"
#include "hooks.h"
#include "puppet.h"

namespace {

using DamageEntryFunction = uint32_t(__fastcall*)(void* self, void* edx, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e);
DamageEntryFunction g_originalDamageEntry = nullptr;

std::atomic<bool> g_installed{false};

// The engine's entry refuses while a script pauses the character; a puppet is paused only during its own move, so
// enemies updating outside it are refused here.
uint32_t __fastcall damageEntryDetour(void* self, void* edx, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    if (puppet::active(reinterpret_cast<uintptr_t>(self))) return 0;
    return g_originalDamageEntry(self, edx, a, b, c, d, e);
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

}  // namespace player_contact
