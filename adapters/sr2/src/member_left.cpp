#include "member_left.h"

#include "cutscene_sync.h"
#include "pause_mask.h"
#include "sr2_addresses.h"

namespace {

using partner_sites::Thunk;

const uintptr_t g_afterLeaveClear = sr2::kPauseLeaveResume;
const uintptr_t g_unpause = sr2::kUnpauseWorld;

// 1 when the world is to be resumed: the leaver was the last one pausing.
uint32_t __cdecl memberLeft(uintptr_t leaver) {
    const bool resumes = pause_mask::afterLeave(leaver);
    cutscene_sync::memberLeft(leaver);
    return resumes ? 1 : 0;
}

// Replaces the clear of the pause byte and the resume in the member left callback; ebx is the leaver.
void __declspec(naked) memberLeftStub() {
    __asm {
        pushad
        push ebx
        call memberLeft
        add esp, 4
        mov dword ptr [esp + 28], eax
        popad
        test eax, eax
        jz done
        call dword ptr [g_unpause]
    done:
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
