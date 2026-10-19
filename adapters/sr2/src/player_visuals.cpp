#include "player_visuals.h"

#include <windows.h>

#include <vector>

#include "code_patch.h"
#include "guest_call.h"
#include "log.h"
#include "memory.h"
#include "player_sites.h"
#include "player_visuals_rules.h"
#include "remote_players.h"
#include "sr2_addresses.h"

namespace {

using partner_sites::Thunk;

constexpr size_t kMaxRemotes = partner_rules::kMaxWalked;

bool g_trace = false;
const uintptr_t g_hideResume = sr2::kCutsceneStartHideResume;
const uintptr_t g_restoreResume = sr2::kCutsceneEndRestoreResume;

// The remote player humans in list order (the list walk, not the pairing: a human whose member is not known yet is hidden too).
size_t remoteHumans(uintptr_t* out) {
    remote_players::Snapshot snapshot;
    remote_players::take(snapshot);
    size_t count = 0;
    for (size_t i = 0; i < snapshot.humanCount && count < kMaxRemotes; ++i) out[count++] = snapshot.humans[i].human;
    return count;
}

// The game's own treatment of the one human the cutscene start hid (0x6C0526): hide it locally, then kill it off unless it is a live
// human that is not down.
void hide(uintptr_t human) {
    guest_call::call(sr2::kHideHuman, {.ecx = static_cast<uint32_t>(human)}, {1, 1, 0});  // hidden, arg, force
    const uint32_t handle = readDword(human + sr2::kObjectHandleOffset);
    const bool resolves = handle != 0 && remote_players::objectOfHandle(handle) == human;
    const uint32_t healthBits = readDword(human + sr2::kHumanHealthOffset);
    if (player_visuals_rules::needsKill(resolves, healthBits, remote_players::downedTimerOf(human))) {
        guest_call::call(sr2::kKillHuman, {}, {static_cast<uint32_t>(human), 1});
    }
}

void __cdecl hideRemotes() {
    uintptr_t humans[kMaxRemotes];
    const size_t count = remoteHumans(humans);
    for (size_t i = 0; i < count; ++i) hide(humans[i]);
    if (g_trace && count > 0) logger::write("trace: cutscene: hid remote humans %s", player_visuals_rules::humansText(humans, count).c_str());
}

// The cutscene end must leave edi = 0 and ebx = the running cutscene (what the replaced block set), whatever else it did.
void __cdecl restoreRemotes(guest_call::Regs* regs) {
    uintptr_t humans[kMaxRemotes];
    const size_t count = remoteHumans(humans);
    for (size_t i = 0; i < count; ++i) {
        dwordField(humans[i], sr2::kHumanVisibilityFlagsOffset) &= ~sr2::kHumanHiddenFlags;
        guest_call::call(sr2::kRestoreHuman, {}, {static_cast<uint32_t>(humans[i])});
    }
    if (count > 0) regs->ebx = readDword(sr2::kCutscene);
    regs->edi = 0;
    if (g_trace && count > 0) logger::write("trace: cutscene: restored remote humans %s", player_visuals_rules::humansText(humans, count).c_str());
}

// Both blocks are jumped out of in the middle of a function and continue after the block; the registers are the game's.
void __declspec(naked) hideRemotesStub() {
    __asm {
        pushad
        call hideRemotes
        popad
        jmp dword ptr [g_hideResume]
    }
}

void __declspec(naked) restoreRemotesStub() {
    __asm {
        pushad
        push esp
        call restoreRemotes
        add esp, 4
        popad
        jmp dword ptr [g_restoreResume]
    }
}

// The immediate of a skin pool site with its dword replaced by `value`.
std::vector<uint8_t> withImmediate(const player_sites::SkinPoolSite& site, uint32_t value) {
    std::vector<uint8_t> bytes = partner_sites::parseHex(site.expected);
    for (size_t i = 0; i < sizeof(value); ++i) bytes[site.immediateOffset + i] = static_cast<uint8_t>(value >> (8 * i));
    return bytes;
}

}  // namespace

namespace player_visuals {

bool installSkinPool(uint32_t maxPlayers) {
    if (maxPlayers <= sr2::kCoopDefaultPlayers) return false;
    std::vector<CodePatch> patches;
    for (const player_sites::SkinPoolSite& site : player_sites::kSkinPool) patches.push_back({site.id, site.address, partner_sites::parseHex(site.expected), {}});
    if (!codeMatches(patches)) {
        logger::write("player visuals: skin pool sites differ, stays at %u", player_visuals_rules::kShippedSkinObjects);
        return false;
    }
    if (readDword(sr2::kSkinFreeList) != 0 || readDword(sr2::kSkinUsedList) != 0 || readDword(sr2::kSkinLevels) != 0) {
        logger::write("player visuals: skin pool already built, stays at %u", player_visuals_rules::kShippedSkinObjects);
        return false;
    }
    void* block = VirtualAlloc(nullptr, maxPlayers * player_visuals_rules::kSkinObjectBytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!block) {
        logger::write("player visuals: skin pool stays at %u, %u objects unavailable", player_visuals_rules::kShippedSkinObjects, maxPlayers);
        return false;
    }
    const uint32_t base = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(block));
    const auto values = player_visuals_rules::skinPoolImmediates(base, maxPlayers);
    for (size_t i = 0; i < patches.size(); ++i) patches[i].replacement = withImmediate(player_sites::kSkinPool[i], values[i]);
    if (!applyCodePatches(patches)) {
        VirtualFree(block, 0, MEM_RELEASE);
        logger::write("player visuals: skin pool stays at %u, the code edits were not applied", player_visuals_rules::kShippedSkinObjects);
        return false;
    }
    logger::write("player visuals: skin pool %u -> %u objects at 0x%08X", player_visuals_rules::kShippedSkinObjects, maxPlayers, base);
    return true;
}

void setTrace(bool trace) { g_trace = trace; }

partner_thunks::Binding bind(Thunk thunk) {
    switch (thunk) {
        case Thunk::HideRemotesAtCutsceneStart: return {reinterpret_cast<void*>(&hideRemotesStub), nullptr};
        case Thunk::RestoreRemotesAtCutsceneEnd: return {reinterpret_cast<void*>(&restoreRemotesStub), nullptr};
        default: return {nullptr, nullptr};
    }
}

}  // namespace player_visuals
