// Replacements of single calls: identity tests, nearest remote, and the calls that must reach every remote.
#include "fpu_guard.h"
#include "guest_call.h"
#include "partner_scope.h"
#include "partner_thunks.h"
#include "remote_players.h"
#include "sr2_addresses.h"
#include "thunk_frame.h"

namespace {

using partner_sites::Thunk;

static_assert(sr2::kObjectPositionOffset == 0x14, "nearestRemoteEdi14Stub assembles this offset");

partner_rules::Vec3 pointAt(uint32_t address) { return *reinterpret_cast<const partner_rules::Vec3*>(address); }

// Identity tests: the compared register holding any remote player counts as "the partner".
uintptr_t __cdecl matchRemote(uintptr_t compared) {
    return remote_players::isRemotePlayerHuman(compared) ? compared : partner_scope::partnerHuman();
}

uintptr_t __cdecl nearestToPoint(uint32_t point) {
    FpuGuard guard;
    return remote_players::nearestRemoteTo(pointAt(point));
}

uintptr_t __cdecl nearestToLocal() {
    FpuGuard guard;
    return remote_players::nearestRemoteTo(remote_players::positionOf(remote_players::localHuman()));
}

void __cdecl lookSyncEach(uintptr_t target, uint32_t localHuman) {
    partner_scope::eachRemoteMember(
        [&](uintptr_t member) { guest_call::call(target, {.ecx = member, .eax = localHuman}); });
}

void __cdecl lookSyncA(uint32_t localHuman) { lookSyncEach(sr2::kLookSyncA, localHuman); }
void __cdecl lookSyncB(uint32_t localHuman) { lookSyncEach(sr2::kLookSyncB, localHuman); }

void __cdecl resyncEachRemote() {
    partner_scope::eachRemoteHuman([](uintptr_t human) { guest_call::call(sr2::kResyncHuman, {.ecx = human}); });
}

void __cdecl holdAssetsEach(uint32_t list) {
    partner_scope::eachRemoteHuman([&](uintptr_t human) { guest_call::call(sr2::kHoldAssets, {}, {human, list}); });
}

PARTNER_REG_STUB(matchRemoteEsiStub, esi, matchRemote)
PARTNER_REG_STUB(matchRemoteEdiStub, edi, matchRemote)
PARTNER_REG_STUB(nearestRemoteEdiStub, edi, nearestToPoint)
PARTNER_REG_STUB(nearestRemoteEsiStub, esi, nearestToPoint)
PARTNER_REG_STUB(lookSyncAStub, eax, lookSyncA)
PARTNER_REG_STUB(lookSyncBStub, eax, lookSyncB)

// edi + 0x14 is the vec3 inside the follower.
void __declspec(naked) nearestRemoteEdi14Stub() {
    __asm {
        lea eax, [edi + 0x14]
        push eax
        call nearestToPoint
        add esp, 4
        ret
    }
}

// cdecl(human, list) is replaced; the caller pops both. The list is the second argument.
void __declspec(naked) holdAssetsStub() {
    __asm {
        push dword ptr [esp + 8]
        call holdAssetsEach
        add esp, 4
        ret
    }
}

}  // namespace

namespace partner_thunks {

Binding bindCalls(Thunk thunk) {
    switch (thunk) {
        case Thunk::MatchRemoteEsi: return {reinterpret_cast<void*>(&matchRemoteEsiStub), nullptr};
        case Thunk::MatchRemoteEdi: return {reinterpret_cast<void*>(&matchRemoteEdiStub), nullptr};
        case Thunk::NearestRemoteEdi: return {reinterpret_cast<void*>(&nearestRemoteEdiStub), nullptr};
        case Thunk::NearestRemoteEsi: return {reinterpret_cast<void*>(&nearestRemoteEsiStub), nullptr};
        case Thunk::NearestRemoteEdi14: return {reinterpret_cast<void*>(&nearestRemoteEdi14Stub), nullptr};
        case Thunk::NearestRemoteLocal: return {reinterpret_cast<void*>(&nearestToLocal), nullptr};
        case Thunk::EachRemoteMember82B9F0: return {reinterpret_cast<void*>(&lookSyncAStub), nullptr};
        case Thunk::EachRemoteMember82BC50: return {reinterpret_cast<void*>(&lookSyncBStub), nullptr};
        case Thunk::EachRemoteHuman9DFE80: return {reinterpret_cast<void*>(&resyncEachRemote), nullptr};
        case Thunk::EachRemoteHuman6BE1E0: return {reinterpret_cast<void*>(&holdAssetsStub), nullptr};
        default: return {nullptr, nullptr};
    }
}

}  // namespace partner_thunks
