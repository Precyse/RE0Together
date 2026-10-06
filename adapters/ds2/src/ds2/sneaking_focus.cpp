// DEATH STRANDING 2: the camps' stealth NPCs (DSSneakingNpcManager and two sibling managers) are put to sleep by their own
// activity update, 0x141bd3d50 (called from the managers' update slots). Each call copies ONE position, EntityManagerGame
// +0xA0 (this machine's player), measures every member of every group to it against the group's near and far radii and
// puts what is beyond to sleep with an inlined SetSleeping. The activity sweep partner_focus splits does not own these
// NPCs: found live by a write watch on a woken enemy, 24 ms after the wake the inlined sleep at 0x141bd43b1 ran.
//
// The partner's body has to count as a focus here too. Running the update twice on split member lists crashed the game at
// the body's spawn (3 of 3 launches), so the update runs once and only its distance changes: the instructions that turn a
// member's position into a squared distance (0x141bd3ffa..0x141bd406e) are replaced by a call that returns the squared
// distance to the nearer of the two foci, and the two instructions of the old block that the next test needs.
#include "ds2/sneaking_focus.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <vector>

#include "decima/safe_read.h"
#include "decima/world_transform.h"
#include "ds2/engine.h"
#include "ds2/entity_wake.h"
#include "ds2/partner_focus.h"
#include "hooks.h"
#include "log.h"

namespace {

constexpr uintptr_t kDistanceBlock = 0x141bd3ffa;  // first instruction of the distance computation
constexpr size_t kDistanceBlockBytes = 0x74;       // up to the `test cl, 1` (0x141bd406e) that uses its result
constexpr uint8_t kBlockStart[] = {0xC5, 0xFB, 0x10, 0x44, 0x24, 0x50};  // vmovsd xmm0, [rsp+0x50]
constexpr uint8_t kBlockEnd[] = {0xC5, 0xE8, 0x58, 0xF1};                // vaddps xmm6, xmm2, xmm1 (the block's last instruction)

constexpr uintptr_t kUpdateBody = 0x141bd3d50;  // (manager*): the activity update of the NPC managers

// The member's acceptance test inside the update: `call [component+0xF0]` then `test al, al`; a member it rejects is treated
// as out of range and slept. Replaced by a jump into the spare bytes of the distance patch that calls acceptsMember.
constexpr uintptr_t kAcceptSite = 0x141bd4094;
constexpr size_t kAcceptSiteBytes = 12;
constexpr uintptr_t kAcceptReturn = 0x141bd40a0;  // the `test al, al` after the call
constexpr uint8_t kAcceptSiteCode[] = {0x48, 0x8B, 0x03, 0x48, 0x8B, 0xCB, 0xFF, 0x90, 0xF0, 0x00, 0x00, 0x00};
constexpr size_t kAcceptSlot = 0xF0 / sizeof(uintptr_t);  // vtable slot of the acceptance test

// Manager fields the update reads per group index (diagnostics only): per-group arrays and the group records.
constexpr uintptr_t kGroupBudgets = 0x30;   // uint32*: awake budget
constexpr uintptr_t kGroupNear = 0x40;      // float*: near radius
constexpr uintptr_t kGroupFar = 0x50;       // float*: far radius
constexpr uintptr_t kGroupLimit = 0x60;     // int32*: second limit (the update uses 1000 when negative)
constexpr uintptr_t kGroupCount = 0x138;    // int32
constexpr uintptr_t kGroups = 0x140;        // Group* (records of 16 bytes {int32 count, pad, Item** items})
constexpr uintptr_t kItemEntity = 0x88;     // Item +0x88: the member's Entity*
constexpr ULONGLONG kLogEveryMs = 5000;

struct Group {
    int32_t count;
    int32_t pad;
    uintptr_t* items;
};
static_assert(sizeof(Group) == 0x10);

void (*g_original)(uintptr_t manager) = nullptr;

// Test switches (test command npcpartner.txt), off by default: the partner's members get their own awake budget / are not
// rejected by the acceptance test. They exist to tell which of the two keeps a member next to the partner asleep.
std::atomic<bool> g_ownBudget{false};
std::atomic<bool> g_skipAcceptance{false};

constexpr uintptr_t kEntityManagerGlobal = 0x14623DEB0;
constexpr uintptr_t kFocusOffset = 0xA0;  // WorldPosition: the point the update measures from

// What the replaced block did, in the registers the code after it expects: rax holds the member's Entity on entry; the
// squared distance goes to xmm6 and cl gets "the owner entity (rbx+0x48) is not asleep" (flags +0x98 bit 9 clear).
//   mov rcx, rax              ; the entity is the helper's argument
//   vzeroupper                ; as the engine does before its own calls
//   mov rax, <helper>         ; call it: the squared distance comes back in xmm0
//   call rax
//   vmovaps xmm6, xmm0
//   mov rax, [rbx+0x48]
//   mov rcx, [rax+0x98]
//   shr rcx, 9
//   not cl
//   jmp over the filler       ; the rest of the old block is never run
constexpr uint8_t kMovRcxRax[] = {0x48, 0x89, 0xC1};
constexpr uint8_t kVzeroupper[] = {0xC5, 0xF8, 0x77};
constexpr uint8_t kMovRaxImm[] = {0x48, 0xB8};
constexpr uint8_t kCallRax[] = {0xFF, 0xD0};
constexpr uint8_t kMoveResult[] = {0xC5, 0xF8, 0x28, 0xF0};
constexpr uint8_t kLoadOwnerFlags[] = {0x48, 0x8B, 0x43, 0x48, 0x48, 0x8B, 0x88, 0x98, 0x00, 0x00, 0x00, 0x48, 0xC1, 0xE9, 0x09, 0xF6, 0xD1};
constexpr uint8_t kMovRcxRbx[] = {0x48, 0x89, 0xD9};
constexpr uint8_t kMovRdxItemEntity[] = {0x48, 0x8B, 0x97, 0x88, 0x00, 0x00, 0x00};  // mov rdx, [rdi+0x88]
constexpr uint8_t kJumpNear = 0xE9;
constexpr uint8_t kNop = 0x90;
constexpr uint8_t kJumpShort = 0xEB;
constexpr uint8_t kFiller = 0xCC;

float squaredDistanceToFocus(const decima::WorldPosition& focus, const decima::WorldPosition& at) {
    const float dx = static_cast<float>(focus.x - at.x), dy = static_cast<float>(focus.y - at.y),
                dz = static_cast<float>(focus.z - at.z);
    return (dy * dy + dx * dx) + dz * dz;
}

// Called from the patched block for each member: the squared distance to the nearer of this machine's player and the
// partner's body, the way the engine computed it (doubles subtracted, then single precision).
float nearestSquaredDistance(uintptr_t entity) {
    decima::WorldPosition at;
    const uintptr_t manager = decima::readPointer(ds2::at(kEntityManagerGlobal));
    if (!decima::safeRead(entity + ds2::kEntityTransform, at) || !manager) return 0.0f;
    float nearest = squaredDistanceToFocus(ds2::field<decima::WorldPosition>(manager, kFocusOffset), at);
    decima::WorldPosition partner;
    if (partner_focus::partnerPosition(partner)) nearest = std::fmin(nearest, squaredDistanceToFocus(partner, at));
    return nearest;
}

// Whether the entity stands nearer the partner's body than this machine's player.
bool nearerPartner(uintptr_t entity) {
    decima::WorldPosition at, partner;
    const uintptr_t manager = decima::readPointer(ds2::at(kEntityManagerGlobal));
    if (!manager || !decima::safeRead(entity + ds2::kEntityTransform, at) || !partner_focus::partnerPosition(partner)) return false;
    return squaredDistanceToFocus(partner, at) < squaredDistanceToFocus(ds2::field<decima::WorldPosition>(manager, kFocusOffset), at);
}

// Called from the patched acceptance site: the engine's own test, unless the switch says a member nearer the partner passes.
bool acceptsMember(uintptr_t component, uintptr_t entity) {
    using Accepts = bool (*)(uintptr_t component);
    const auto accepts = reinterpret_cast<const Accepts*>(decima::readPointer(component))[kAcceptSlot];
    const bool accepted = accepts(component);
    return accepted || (g_skipAcceptance.load() && nearerPartner(entity));
}

constexpr double kPartnerSightMetres = 80.0;  // members this close to the partner get a line of their own
constexpr int kMaxMemberLines = 8;

uintptr_t memberEntity(uintptr_t item) {
    uintptr_t entity = 0;
    return decima::safeRead(item + kItemEntity, entity) ? entity : 0;
}

// How many members of the group are nearer (to the nearer focus) than this one: its place in the update's sorted list.
int rankInGroup(const Group& group, uintptr_t entity) {
    const float mine = nearestSquaredDistance(entity);
    int rank = 0;
    for (int i = 0; i < group.count; ++i) {
        const uintptr_t other = memberEntity(group.items[i]);
        if (other && other != entity && nearestSquaredDistance(other) < mine) ++rank;
    }
    return rank;
}

// Diagnostics: the members of a group that stand near the partner, with their place in the group's sorted list against its budget.
void logPartnerMembers(int index, const Group& group, uint32_t budget, int& lines) {
    decima::WorldPosition partner;
    if (!partner_focus::partnerPosition(partner)) return;
    for (int i = 0; i < group.count && lines < kMaxMemberLines; ++i) {
        const uintptr_t entity = memberEntity(group.items[i]);
        decima::WorldPosition at;
        if (!entity || !decima::safeRead(entity + ds2::kEntityTransform, at)) continue;
        const double metres = std::sqrt(partner_focus::squaredDistance(at, partner));
        if (metres > kPartnerSightMetres) continue;
        ++lines;
        logger::write("sneaking_focus: group %d member %p, %.0f m from the partner, %s, rank %d of %d against budget %u", index,
                      reinterpret_cast<void*>(entity), metres, ds2::entityAsleep(entity) ? "asleep" : "awake",
                      rankInGroup(group, entity), group.count, budget);
    }
}

// Diagnostics: after the update, every 5 s, each group's members and how many sleep, with the numbers that decide it.
void logGroups(uintptr_t manager) {
    static ULONGLONG nextLog = 0;
    const ULONGLONG now = GetTickCount64();
    if (now < nextLog) return;
    nextLog = now + kLogEveryMs;
    const auto groups = ds2::field<const Group*>(manager, kGroups);
    const int count = ds2::field<int32_t>(manager, kGroupCount);
    const auto nearRadius = ds2::field<const float*>(manager, kGroupNear);
    const auto farRadius = ds2::field<const float*>(manager, kGroupFar);
    const auto limit = ds2::field<const int32_t*>(manager, kGroupLimit);
    const auto budget = ds2::field<const uint32_t*>(manager, kGroupBudgets);
    int memberLines = 0;
    for (int g = 0; g < count; ++g) {
        int asleep = 0;
        for (int i = 0; i < groups[g].count; ++i) {
            uintptr_t entity = 0;
            if (decima::safeRead(groups[g].items[i] + kItemEntity, entity) && entity && ds2::entityAsleep(entity)) ++asleep;
        }
        logger::write("sneaking_focus: manager %p group %d: %d members, %d asleep, near %.0f far %.0f, limit %d, budget %u",
                      reinterpret_cast<void*>(manager), g, groups[g].count, asleep, nearRadius[g], farRadius[g], limit[g], budget[g]);
        logPartnerMembers(g, groups[g], budget[g], memberLines);
    }
}

// With the budget switch on, the partner's members come on top of the budgets: every group's budget is doubled for the call.
void runWithOwnBudget(uintptr_t manager) {
    decima::WorldPosition partner;
    if (!g_ownBudget.load() || !partner_focus::partnerPosition(partner)) {
        g_original(manager);
        return;
    }
    const int count = ds2::field<int32_t>(manager, kGroupCount);
    auto* budget = ds2::field<uint32_t*>(manager, kGroupBudgets);
    const std::vector<uint32_t> saved(budget, budget + count);
    for (int g = 0; g < count; ++g) budget[g] *= 2;
    g_original(manager);
    std::copy(saved.begin(), saved.end(), budget);
}

void updateDetour(uintptr_t manager) {
    runWithOwnBudget(manager);
    logGroups(manager);
}

bool blockIsAsExpected(const uint8_t* block) {
    return std::memcmp(block, kBlockStart, sizeof(kBlockStart)) == 0 &&
           std::memcmp(block + kDistanceBlockBytes - sizeof(kBlockEnd), kBlockEnd, sizeof(kBlockEnd)) == 0 &&
           std::memcmp(reinterpret_cast<const void*>(ds2::at(kAcceptSite)), kAcceptSiteCode, sizeof(kAcceptSiteCode)) == 0;
}

void writeCode(void* address, const uint8_t* bytes, size_t count) {
    DWORD protection = 0;
    VirtualProtect(address, count, PAGE_EXECUTE_READWRITE, &protection);
    std::memcpy(address, bytes, count);
    VirtualProtect(address, count, protection, &protection);
    FlushInstructionCache(GetCurrentProcess(), address, count);
}

// Appends a near jump from `from` (the address its opcode will have) to `target`.
uint8_t* appendJump(uint8_t* to, uintptr_t from, uintptr_t target) {
    *to++ = kJumpNear;
    const int32_t displacement = static_cast<int32_t>(target - (from + 5));
    std::memcpy(to, &displacement, sizeof(displacement));
    return to + sizeof(displacement);
}

// Appends `bytes` to the patch under construction.
uint8_t* append(uint8_t* to, const uint8_t* bytes, size_t count) {
    std::memcpy(to, bytes, count);
    return to + count;
}

void writePatch(uint8_t* block) {
    uint8_t patch[kDistanceBlockBytes];
    std::memset(patch, kFiller, sizeof(patch));
    uint8_t* at = patch;
    at = append(at, kMovRcxRax, sizeof(kMovRcxRax));
    at = append(at, kVzeroupper, sizeof(kVzeroupper));
    at = append(at, kMovRaxImm, sizeof(kMovRaxImm));
    const uintptr_t helper = reinterpret_cast<uintptr_t>(&nearestSquaredDistance);
    at = append(at, reinterpret_cast<const uint8_t*>(&helper), sizeof(helper));
    at = append(at, kCallRax, sizeof(kCallRax));
    at = append(at, kMoveResult, sizeof(kMoveResult));
    at = append(at, kLoadOwnerFlags, sizeof(kLoadOwnerFlags));
    constexpr size_t kJumpBytes = 2;
    const size_t used = static_cast<size_t>(at - patch) + kJumpBytes;
    *at++ = kJumpShort;
    *at++ = static_cast<uint8_t>(kDistanceBlockBytes - used);  // lands on the `test cl, 1` after the block

    // The spare bytes after that jump hold the acceptance call: acceptsMember(component = rbx, entity = [rdi+0x88]), whose
    // result the engine's `test al, al` reads.
    const uintptr_t cave = ds2::at(kDistanceBlock) + static_cast<size_t>(at - patch);
    at = append(at, kMovRcxRbx, sizeof(kMovRcxRbx));
    at = append(at, kMovRdxItemEntity, sizeof(kMovRdxItemEntity));
    at = append(at, kVzeroupper, sizeof(kVzeroupper));
    at = append(at, kMovRaxImm, sizeof(kMovRaxImm));
    const uintptr_t accepts = reinterpret_cast<uintptr_t>(&acceptsMember);
    at = append(at, reinterpret_cast<const uint8_t*>(&accepts), sizeof(accepts));
    at = append(at, kCallRax, sizeof(kCallRax));
    appendJump(at, ds2::at(kDistanceBlock) + static_cast<size_t>(at - patch), ds2::at(kAcceptReturn));

    uint8_t site[kAcceptSiteBytes];
    std::memset(site, kNop, sizeof(site));
    appendJump(site, ds2::at(kAcceptSite), cave);

    writeCode(block, patch, kDistanceBlockBytes);
    writeCode(reinterpret_cast<void*>(ds2::at(kAcceptSite)), site, sizeof(site));
}

}  // namespace

namespace sneaking_focus {

void setSwitches(bool ownBudget, bool skipAcceptance) {
    g_ownBudget = ownBudget;
    g_skipAcceptance = skipAcceptance;
    logger::write("sneaking_focus: own budget %s, acceptance test skipped for the partner's members %s", ownBudget ? "on" : "off",
                  skipAcceptance ? "on" : "off");
}

void installEarly(bool diagnostics, bool testCommands) {
    uint8_t* block = reinterpret_cast<uint8_t*>(ds2::at(kDistanceBlock));
    if (!blockIsAsExpected(block)) {
        logger::write("sneaking_focus: the NPC managers' distance block is not the expected code, left alone");
        return;
    }
    writePatch(block);
    if (diagnostics || testCommands) {
        hooks::install("npc manager update (diagnostics, test switches)", ds2::at(kUpdateBody), reinterpret_cast<void*>(&updateDetour),
                       reinterpret_cast<void**>(&g_original));
    }
    logger::write("sneaking_focus: the NPC managers' distance is the nearer of the player and the partner");
}

}  // namespace sneaking_focus
