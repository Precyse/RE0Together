#include "cutscene_sync.h"

#include "activity_state.h"
#include "cutscene_sync_rules.h"
#include "guest_call.h"
#include "memory.h"
#include "sr2_addresses.h"
#include "thunk_frame.h"

namespace {

using cutscene_sync_rules::Handshake;
using partner_sites::Thunk;

constexpr size_t kPayloadArg = 0;
constexpr size_t kContextArg = 1;

void* g_handlerOriginal = nullptr;
cutscene_sync_rules::SenderSets g_senders;

uint8_t& byteAt(uintptr_t address) { return *reinterpret_cast<uint8_t*>(address); }

bool skippedTogether() {
    const uintptr_t cutscene = readDword(sr2::kCutscene);
    return cutscene && byteAt(cutscene + sr2::kCutsceneSharedSkipOffset) != 0;
}

// The bytes a handshake sets, saved and put back while some remote has not sent it yet. Sub 3 sets "skipped" as well.
struct Bytes {
    uint8_t* first;
    uint8_t* second;  // may be null
};

Bytes bytesOf(Handshake handshake) {
    switch (handshake) {
        case Handshake::Ready: return {&byteAt(sr2::kCutsceneRemoteReady), nullptr};
        case Handshake::Skip: return {&byteAt(sr2::kCutsceneRemoteSkipped), nullptr};
        default: return {&byteAt(sr2::kCutsceneRemoteDone), &byteAt(sr2::kCutsceneRemoteSkipped)};
    }
}

// MC1: cdecl(payload, context), the payload starts with the sub id.
void __cdecl cutsceneAllRemotes(GuestFrame* frame) {
    const uint32_t* args = frame->args;
    const uintptr_t original = reinterpret_cast<uintptr_t>(g_handlerOriginal);
    const Handshake handshake = cutscene_sync_rules::handshakeOf(static_cast<int8_t>(byteAt(args[kPayloadArg])), skippedTogether());
    if (handshake == Handshake::None) {
        frame->regs.eax = guest_call::call(original, frame->regs, {args[kPayloadArg], args[kContextArg]});
        return;
    }
    const Bytes bytes = bytesOf(handshake);
    const uint8_t firstBefore = *bytes.first;
    const uint8_t secondBefore = bytes.second ? *bytes.second : 0;
    g_senders.add(handshake, activity_state::senderOf(args[kContextArg]));
    frame->regs.eax = guest_call::call(original, frame->regs, {args[kPayloadArg], args[kContextArg]});
    uintptr_t required[cutscene_sync_rules::kMaxMembers];
    const size_t count = activity_state::remoteMembersInGame(required);
    if (g_senders.complete(handshake, required, count)) return;
    *bytes.first = firstBefore;
    if (bytes.second) *bytes.second = secondBefore;
}

PARTNER_WRAP_STUB(cutsceneAllRemotesStub, cutsceneAllRemotes, 0)

// MC2: the game's own `mov byte [0x2527D1E], 0` (this machine has not asked to skip), and the senders forgotten.
void __cdecl startCutscene() {
    byteAt(sr2::kCutsceneLocalSkipped) = 0;
    g_senders.clear();
}

// Replaces that store where the game clears its flags for a cutscene that starts here. Registers and flags are kept, and
// the x87 stack is not touched (a value waits on it).
void __declspec(naked) cutsceneSetsResetStub() {
    __asm {
        pushfd
        pushad
        call startCutscene
        popad
        popfd
        ret
    }
}

}  // namespace

namespace cutscene_sync {

partner_thunks::Binding bind(Thunk thunk) {
    switch (thunk) {
        case Thunk::CutsceneAllRemotes: return {reinterpret_cast<void*>(&cutsceneAllRemotesStub), &g_handlerOriginal};
        case Thunk::CutsceneSetsReset: return {reinterpret_cast<void*>(&cutsceneSetsResetStub), nullptr};
        default: return {nullptr, nullptr};
    }
}

}  // namespace cutscene_sync
