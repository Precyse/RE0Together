#include "member_left.h"

#include "cutscene_sync.h"
#include "net_handlers.h"
#include "pause_mask.h"
#include "sr2_addresses.h"

namespace {

using partner_sites::Thunk;

const uintptr_t g_afterLeaveClear = sr2::kPauseLeaveResume;

void __cdecl memberLeft(uintptr_t leaver) {
    pause_mask::afterLeave(leaver);
    cutscene_sync::memberLeft(leaver);
    net_handlers::memberLeft(leaver);
}

// Replaces the clear of the pause byte and the resume in the member left callback; ebx is the leaver.
void __declspec(naked) memberLeftStub() {
    __asm {
        pushad
        push ebx
        call memberLeft
        add esp, 4
        popad
        jmp dword ptr [g_afterLeaveClear]
    }
}

}  // namespace

namespace member_left {

partner_thunks::Binding bind(Thunk thunk) {
    switch (thunk) {
        case Thunk::MemberLeft: return {reinterpret_cast<void*>(&memberLeftStub), nullptr};
        default: return {nullptr, nullptr};
    }
}

}  // namespace member_left
