// DEATH STRANDING 2: the camps' stealth NPCs (DSSneakingNpcManager and two sibling managers) are put to sleep by their own
// activity update, 0x141bd3d50 (called from the managers' update slots). Each call copies ONE position, EntityManagerGame
// +0xA0 (this machine's player), measures every member of every group to it against the group's near and far radii and
// puts what is beyond to sleep with an inlined SetSleeping. The activity sweep partner_focus splits does not own these
// NPCs: found live by a write watch on a woken enemy, 24 ms after the wake the inlined sleep at 0x141bd43b1 ran.
//
// The partner's body has to count as a focus here too. Running the update twice on split member lists crashed the game at
// the body's spawn (3 of 3 launches), so the update runs once and only its decision changes: the instructions that turn a
// member's position into a squared distance (0x141bd3ffa..0x141bd406e) are replaced by a call that returns the squared
// distance to the nearer of the two foci, and the two instructions of the old block that the next test needs. The stub
// lives in the 116 bytes of the old distance block, followed by a short jump over the rest.
#include "ds2/sneaking_focus.h"

#include <windows.h>

#include <cmath>
#include <cstring>

#include "decima/safe_read.h"
#include "decima/world_transform.h"
#include "ds2/engine.h"
#include "ds2/partner_focus.h"
#include "log.h"

namespace {

constexpr uintptr_t kDistanceBlock = 0x141bd3ffa;  // first instruction of the distance computation
constexpr size_t kDistanceBlockBytes = 0x74;       // up to the `test cl, 1` (0x141bd406e) that uses its result
constexpr uint8_t kBlockStart[] = {0xC5, 0xFB, 0x10, 0x44, 0x24, 0x50};  // vmovsd xmm0, [rsp+0x50]
constexpr uint8_t kBlockEnd[] = {0xC5, 0xE8, 0x58, 0xF1};                // vaddps xmm6, xmm2, xmm1 (the block's last instruction)

constexpr uintptr_t kEntityManagerGlobal = 0x14623DEB0;
constexpr uintptr_t kFocusOffset = 0xA0;  // WorldPosition: the point the update measures from
// What the distance stub does, in the registers the code after the old block expects: rax holds the member's Entity on entry;
// the squared distance goes to xmm6 and cl gets "the owner entity (rbx+0x48) is not asleep" (flags +0x98 bit 9 clear).
//   mov rcx, rax / vzeroupper / mov rax, <nearestSquaredDistance> / call rax / vmovaps xmm6, xmm0
//   mov rax, [rbx+0x48] / mov rcx, [rax+0x98] / shr rcx, 9 / not cl / jmp over the rest
constexpr uint8_t kMovRcxRax[] = {0x48, 0x89, 0xC1};
constexpr uint8_t kVzeroupper[] = {0xC5, 0xF8, 0x77};
constexpr uint8_t kMovRaxImm[] = {0x48, 0xB8};
constexpr uint8_t kCallRax[] = {0xFF, 0xD0};
constexpr uint8_t kMoveResult[] = {0xC5, 0xF8, 0x28, 0xF0};
constexpr uint8_t kLoadOwnerFlags[] = {0x48, 0x8B, 0x43, 0x48, 0x48, 0x8B, 0x88, 0x98, 0x00, 0x00, 0x00, 0x48, 0xC1, 0xE9, 0x09, 0xF6, 0xD1};
constexpr uint8_t kJumpShort = 0xEB;
constexpr uint8_t kFiller = 0xCC;

float squaredDistanceToFocus(const decima::WorldPosition& focus, const decima::WorldPosition& at) {
    const float dx = static_cast<float>(focus.x - at.x), dy = static_cast<float>(focus.y - at.y),
                dz = static_cast<float>(focus.z - at.z);
    return (dy * dy + dx * dx) + dz * dz;
}

bool codeIsAsExpected(const uint8_t* block) {
    return std::memcmp(block, kBlockStart, sizeof(kBlockStart)) == 0 &&
           std::memcmp(block + kDistanceBlockBytes - sizeof(kBlockEnd), kBlockEnd, sizeof(kBlockEnd)) == 0;
}

void writeCode(void* address, const uint8_t* bytes, size_t count) {
    DWORD protection = 0;
    VirtualProtect(address, count, PAGE_EXECUTE_READWRITE, &protection);
    std::memcpy(address, bytes, count);
    VirtualProtect(address, count, protection, &protection);
    FlushInstructionCache(GetCurrentProcess(), address, count);
}

// Appends `bytes` to the code under construction.
uint8_t* append(uint8_t* to, const uint8_t* bytes, size_t count) {
    std::memcpy(to, bytes, count);
    return to + count;
}

// Appends `mov rax, function / call rax`.
uint8_t* appendCall(uint8_t* to, uintptr_t function) {
    to = append(to, kMovRaxImm, sizeof(kMovRaxImm));
    to = append(to, reinterpret_cast<const uint8_t*>(&function), sizeof(function));
    return append(to, kCallRax, sizeof(kCallRax));
}

void writePatches() {
    uint8_t block[kDistanceBlockBytes];
    std::memset(block, kFiller, sizeof(block));
    uint8_t* at = block;
    at = append(at, kMovRcxRax, sizeof(kMovRcxRax));
    at = append(at, kVzeroupper, sizeof(kVzeroupper));
    at = appendCall(at, reinterpret_cast<uintptr_t>(&sneaking_focus::nearestSquaredDistance));
    at = append(at, kMoveResult, sizeof(kMoveResult));
    at = append(at, kLoadOwnerFlags, sizeof(kLoadOwnerFlags));
    constexpr size_t kJumpShortBytes = 2;
    const size_t afterJump = static_cast<size_t>(at - block) + kJumpShortBytes;
    *at++ = kJumpShort;
    *at++ = static_cast<uint8_t>(kDistanceBlockBytes - afterJump);  // lands on the `test cl, 1` after the old block
    writeCode(reinterpret_cast<void*>(ds2::at(kDistanceBlock)), block, sizeof(block));
}

}  // namespace

namespace sneaking_focus {

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

void installEarly() {
    if (!codeIsAsExpected(reinterpret_cast<const uint8_t*>(ds2::at(kDistanceBlock)))) {
        logger::write("sneaking_focus: the NPC managers' update is not the expected code, left alone");
        return;
    }
    writePatches();
    logger::write("sneaking_focus: the NPC managers' distance is the nearer of the player and the partner");
}

}  // namespace sneaking_focus
