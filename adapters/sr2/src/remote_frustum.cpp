#include "remote_frustum.h"

#include <cstring>
#include <mutex>

#include "fpu_guard.h"
#include "frustum_rules.h"
#include "guest_call.h"
#include "memory.h"
#include "remote_players.h"
#include "sr2_addresses.h"
#include "thunk_frame.h"

namespace {

using partner_sites::Thunk;

constexpr uintptr_t kFrustumTest = 0xA020C0;    // ECX, EDX = the box, cdecl(frustum): AL = visible
constexpr uintptr_t kHumanView = 0x9DFA00;      // ECX = human, EAX = vec3 out, cdecl(9 dwords out): its camera
constexpr uintptr_t kHumanViewAngleA = 0x12D0;  // float, in a human: the two arguments after the camera
constexpr uintptr_t kHumanViewAngleB = 0x12D4;
constexpr size_t kOrientationWords = 9;

void* g_buildOriginal = nullptr;
std::mutex g_lock;
alignas(16) uint8_t g_frustums[partner_rules::kMaxWalked][frustum_rules::kFrustumBytes];
size_t g_count = 0;

uint32_t addressOf(const void* pointer) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pointer)); }

// W3b: cdecl(position*, orientation*, float, float). Runs the game's build, then builds one frustum per remote the way the
// render frame does for the partner.
void __cdecl buildRemoteFrustums(GuestFrame* frame) {
    const uint32_t* args = frame->args;
    const uintptr_t original = reinterpret_cast<uintptr_t>(g_buildOriginal);
    guest_call::call(original, frame->regs, {args[0], args[1], args[2], args[3]});

    remote_players::Snapshot snapshot;
    remote_players::take(snapshot);
    uintptr_t humans[partner_rules::kMaxWalked];
    size_t count = 0;
    for (size_t i = 0; i < snapshot.pairing.count; ++i) {
        const uintptr_t human = snapshot.pairing.items[i].human;
        if (human && !remote_players::isJoining(human)) humans[count++] = human;
    }
    std::lock_guard<std::mutex> lock(g_lock);
    if (count < frustum_rules::kBuildRemotesAtLeast) {
        g_count = 0;
        return;
    }
    FpuGuard guard;
    alignas(16) uint8_t partner[frustum_rules::kFrustumBytes];
    memcpy(partner, reinterpret_cast<const void*>(frustum_rules::kPartnerFrustum), sizeof(partner));
    for (size_t i = 0; i < count; ++i) {
        float position[3];
        uint32_t orientation[kOrientationWords];
        guest_call::call(kHumanView, {.ecx = static_cast<uint32_t>(humans[i]), .eax = addressOf(position)}, {addressOf(orientation)});
        guest_call::call(original, {},
                         {addressOf(position), addressOf(orientation), readDword(humans[i] + kHumanViewAngleA),
                          readDword(humans[i] + kHumanViewAngleB)});
        memcpy(g_frustums[i], reinterpret_cast<const void*>(frustum_rules::kPartnerFrustum), frustum_rules::kFrustumBytes);
    }
    g_count = count;
    memcpy(reinterpret_cast<void*>(frustum_rules::kPartnerFrustum), partner, sizeof(partner));
}

// W3d-h: replaces `call 0xA020C0` (ecx, edx = the box, the frustum pushed, which the caller pops).
void __cdecl frustumTestAnyRemote(GuestFrame* frame) {
    const uint32_t frustum = frame->args[0];
    std::lock_guard<std::mutex> lock(g_lock);
    if (!frustum_rules::asksEveryRemote(frustum, g_count)) {
        frame->regs.eax = guest_call::call(kFrustumTest, frame->regs, {frustum});
        return;
    }
    uint32_t visible = 0;
    for (size_t i = 0; i < g_count && !visible; ++i) {
        visible = guest_call::call(kFrustumTest, frame->regs, {addressOf(g_frustums[i])}) & 0xFF;
    }
    frame->regs.eax = visible;
}

PARTNER_WRAP_STUB(buildStub, buildRemoteFrustums, 0)
PARTNER_WRAP_STUB(testStub, frustumTestAnyRemote, 0)

}  // namespace

namespace remote_frustum {

partner_thunks::Binding bind(Thunk thunk) {
    switch (thunk) {
        case Thunk::BuildRemoteFrustums: return {reinterpret_cast<void*>(&buildStub), &g_buildOriginal};
        case Thunk::FrustumTestAnyRemote: return {reinterpret_cast<void*>(&testStub), nullptr};
        default: return {nullptr, nullptr};
    }
}

}  // namespace remote_frustum
