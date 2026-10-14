// Thunks of credit and targeting in the activities: kills and hits count for every remote player, the nearest player is
// the one that matters, and a message about a player goes to the machine that owns it.
#include "activity_state.h"
#include "activity_thunks.h"
#include "guest_call.h"
#include "log.h"
#include "memory.h"
#include "partner_identity.h"
#include "partner_scope.h"
#include "remote_players.h"
#include "sr2_addresses.h"
#include "thunk_frame.h"

namespace {

using partner_sites::Thunk;

static_assert(sr2::kObjectPositionOffset == 0x14, "the nearest stubs assemble this offset");

// The remote the last identity test of this thread matched, for the message that follows it.
thread_local uintptr_t t_lastMatched = 0;

// The identity test that remembers its answer (AV8).
uintptr_t __cdecl matchAndRemember(uintptr_t compared) {
    t_lastMatched = partner_identity::match(compared);
    return t_lastMatched;
}

// A message goes to the member that owns `human`; nothing is sent when the human has no member.
void sendToOwnerOf(uintptr_t human, uint32_t buffer, uint32_t length) {
    const uintptr_t member = remote_players::memberOfHuman(human);
    if (activity_state::tracing()) {
        logger::write("trace: message sent to human 0x%X member 0x%X", static_cast<unsigned>(human),
                      static_cast<unsigned>(member));
    }
    if (member) guest_call::call(sr2::kSendToMember, {.esi = member}, {buffer, length, 0, 0});
}

// The word at `offset` bytes past the thunk's return address: the stack of the patched function as the call left it.
uint32_t stackAt(const GuestFrame* frame, uintptr_t offset) {
    return readDword(reinterpret_cast<uintptr_t>(&frame->returnAddress) + offset);
}

// AV5: the credit goes to the owner of the attacker at [esp+0x568] of the call site.
void __cdecl sendToOwnerOfStackHuman(GuestFrame* frame) {
    sendToOwnerOf(stackAt(frame, 0x56C), frame->args[0], frame->args[1]);
}

// AV7: the credit goes to the owner of the human in ebp.
void __cdecl sendToOwnerOfEbp(GuestFrame* frame) { sendToOwnerOf(frame->regs.ebp, frame->args[0], frame->args[1]); }

// AV9: the bonus goes to the owner of the remote the matching test found; the broadcast when no test ran.
void __cdecl sendToLastMatched(GuestFrame* frame) {
    const uintptr_t matched = t_lastMatched;
    t_lastMatched = 0;
    if (matched) {
        sendToOwnerOf(matched, frame->args[0], frame->args[1]);
    } else {
        guest_call::call(sr2::kSendBroadcast, {.edi = frame->regs.edi}, {frame->args[0], frame->args[1]});
    }
}

// AC11: the remote nearest to the celeb of Crowd Control, else the default partner.
uintptr_t __cdecl nearestToCeleb(uintptr_t security) {
    const uintptr_t owner = readDword(security + sr2::kSecurityCelebOwnerOffset);
    const uintptr_t celebGetter = owner ? readDword(readDword(owner) + sr2::kSecurityCelebSlot) : 0;
    if (!celebGetter) return partner_scope::partnerHuman();
    const uintptr_t celeb = guest_call::call(celebGetter, {.ecx = owner});
    const uintptr_t nearest = celeb ? partner_identity::nearest(static_cast<uint32_t>(celeb + sr2::kObjectPositionOffset)) : 0;
    return nearest ? nearest : partner_scope::partnerHuman();
}

PARTNER_REG_STUB(matchRemoteEbxStub, ebx, partner_identity::match)
PARTNER_REG_STUB(matchRemoteEbpStub, ebp, partner_identity::match)
PARTNER_REG_STUB(matchRemoteEcxStub, ecx, partner_identity::match)
PARTNER_REG_STUB(matchRememberEsiStub, esi, matchAndRemember)
PARTNER_REG_STUB(nearestRemoteEbxStub, ebx, partner_identity::nearest)
PARTNER_REG_STUB(nearestRemoteEbpStub, ebp, partner_identity::nearest)
PARTNER_REG_STUB(nearestToCelebStub, ebx, nearestToCeleb)

// The compared human is a stack slot of the patched function: [esp + N] at the call site, [esp + N + 4] in the thunk.
void __declspec(naked) matchRemoteEsp560Stub() {
    __asm {
        push dword ptr [esp + 0x564]
        call partner_identity::match
        add esp, 4
        ret
    }
}

void __declspec(naked) matchRemoteStack18Stub() {
    __asm {
        push dword ptr [esp + 0x1C]
        call partner_identity::match
        add esp, 4
        ret
    }
}

void __declspec(naked) matchRemoteStack14Stub() {
    __asm {
        push dword ptr [esp + 0x18]
        call partner_identity::match
        add esp, 4
        ret
    }
}

// edi/esi + 0x14 is the vec3 inside the object.
void __declspec(naked) nearestRemoteEsi14Stub() {
    __asm {
        lea eax, [esi + 0x14]
        push eax
        call partner_identity::nearest
        add esp, 4
        ret
    }
}

// The zombie at [[esp + 0x1C] + 0x14] of the caller's frame, which is [[esp + 0x20] + 0x14] in the thunk.
void __declspec(naked) nearestRemoteStack1c14Stub() {
    __asm {
        mov eax, [esp + 0x20]
        add eax, 0x14
        push eax
        call partner_identity::nearest
        add esp, 4
        ret
    }
}

PARTNER_WRAP_STUB(sendToOwnerOfStackHumanStub, sendToOwnerOfStackHuman, 0)
PARTNER_WRAP_STUB(sendToOwnerOfEbpStub, sendToOwnerOfEbp, 0)
PARTNER_WRAP_STUB(sendToLastMatchedStub, sendToLastMatched, 0)

}  // namespace

namespace activity_thunks {

partner_thunks::Binding bindCredit(Thunk thunk) {
    switch (thunk) {
        case Thunk::MatchRemoteEbx: return {reinterpret_cast<void*>(&matchRemoteEbxStub), nullptr};
        case Thunk::MatchRemoteEbp: return {reinterpret_cast<void*>(&matchRemoteEbpStub), nullptr};
        case Thunk::MatchRemoteEcx: return {reinterpret_cast<void*>(&matchRemoteEcxStub), nullptr};
        case Thunk::MatchRemoteEsp560: return {reinterpret_cast<void*>(&matchRemoteEsp560Stub), nullptr};
        case Thunk::MatchRemoteStack14: return {reinterpret_cast<void*>(&matchRemoteStack14Stub), nullptr};
        case Thunk::MatchRemoteStack18: return {reinterpret_cast<void*>(&matchRemoteStack18Stub), nullptr};
        case Thunk::MatchRemoteRememberEsi: return {reinterpret_cast<void*>(&matchRememberEsiStub), nullptr};
        case Thunk::SendToLastMatched: return {reinterpret_cast<void*>(&sendToLastMatchedStub), nullptr};
        case Thunk::SendToHumanOwnerEsp568: return {reinterpret_cast<void*>(&sendToOwnerOfStackHumanStub), nullptr};
        case Thunk::SendToHumanOwnerEbp: return {reinterpret_cast<void*>(&sendToOwnerOfEbpStub), nullptr};
        case Thunk::NearestRemoteEbx: return {reinterpret_cast<void*>(&nearestRemoteEbxStub), nullptr};
        case Thunk::NearestRemoteEbp: return {reinterpret_cast<void*>(&nearestRemoteEbpStub), nullptr};
        case Thunk::NearestRemoteEsi14: return {reinterpret_cast<void*>(&nearestRemoteEsi14Stub), nullptr};
        case Thunk::NearestRemoteStack1c14: return {reinterpret_cast<void*>(&nearestRemoteStack1c14Stub), nullptr};
        case Thunk::NearestRemoteToCelebEbx: return {reinterpret_cast<void*>(&nearestToCelebStub), nullptr};
        default: return {nullptr, nullptr};
    }
}

}  // namespace activity_thunks
