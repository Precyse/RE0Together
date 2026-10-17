#include "cutscene_sync.h"

#include <mutex>

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
std::mutex g_mutex;  // the handler, the cutscene start and the member left callback are expected on one thread; the lock is cheap
cutscene_sync_rules::SenderSets g_senders;
thread_local bool t_insideStart = false;  // the handler of sub 0 is running the start

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

// Runs the game's handler for the message. A start runs with t_insideStart set, so that the cutscene start the handler runs
// (MC2) keeps the ready set, as the game keeps the byte it saves around the start.
uint32_t runHandler(const GuestFrame* frame, bool isStart) {
    const uint32_t* args = frame->args;
    t_insideStart = isStart;
    const uint32_t result = guest_call::call(reinterpret_cast<uintptr_t>(g_handlerOriginal), frame->regs, {args[kPayloadArg], args[kContextArg]});
    t_insideStart = false;
    return result;
}

// MC1: cdecl(payload, context), the payload starts with the sub id.
void __cdecl cutsceneAllRemotes(GuestFrame* frame) {
    const uint32_t* args = frame->args;
    const int8_t sub = static_cast<int8_t>(byteAt(args[kPayloadArg]));
    const Handshake handshake = cutscene_sync_rules::handshakeOf(sub, skippedTogether());
    if (handshake == Handshake::None) {
        frame->regs.eax = runHandler(frame, cutscene_sync_rules::isStart(sub));
        return;
    }
    const Bytes bytes = bytesOf(handshake);
    const uint8_t firstBefore = *bytes.first;
    const uint8_t secondBefore = bytes.second ? *bytes.second : 0;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_senders.add(handshake, activity_state::senderOf(args[kContextArg]));
    }
    frame->regs.eax = runHandler(frame, false);
    uintptr_t required[cutscene_sync_rules::kMaxMembers];
    const size_t count = activity_state::remoteMembersInGame(required);
    bool complete = false;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        complete = g_senders.complete(handshake, required, count);
        g_senders.setWithheld(handshake, !complete);
    }
    if (complete) return;
    *bytes.first = firstBefore;
    if (bytes.second) *bytes.second = secondBefore;
}

PARTNER_WRAP_STUB(cutsceneAllRemotesStub, cutsceneAllRemotes, 0)

// MC2: the game's own `mov byte [0x2527D1E], 0` (this machine has not asked to skip), and the sets forgotten.
void __cdecl startCutscene() {
    byteAt(sr2::kCutsceneLocalSkipped) = 0;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_senders.started(t_insideStart);
}

// CS1, CS2: the game's own `mov byte [0x2527D1D], 0` (the playback started, or the wait for the remotes gave up), and the
// ready set forgotten with the byte.
void __cdecl consumeRemoteReady() {
    byteAt(sr2::kCutsceneRemoteReady) = 0;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_senders.clearReady();
}

// Replace those stores. Registers and flags are kept, and the x87 stack is not touched (a value waits on it).
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

void __declspec(naked) cutsceneReadyConsumedStub() {
    __asm {
        pushfd
        pushad
        call consumeRemoteReady
        popad
        popfd
        ret
    }
}

// What the game's handler would have written if the leaver's message had been the last one it waited for.
void writeCompleted(const cutscene_sync_rules::SenderSets::Completed& completed) {
    if (completed.ready) byteAt(sr2::kCutsceneRemoteReady) = 1;
    if (completed.skip) byteAt(sr2::kCutsceneRemoteSkipped) = 1;
    if (completed.done) {
        byteAt(sr2::kCutsceneRemoteDone) = 1;
        byteAt(sr2::kCutsceneRemoteSkipped) = 1;
    }
}

}  // namespace

namespace cutscene_sync {

void memberLeft(uintptr_t leaver) {
    uintptr_t required[cutscene_sync_rules::kMaxMembers];
    const size_t count = activity_state::remoteMembersInGame(required);
    std::lock_guard<std::mutex> lock(g_mutex);
    const cutscene_sync_rules::SenderSets::Completed completed = g_senders.memberLeft(leaver, required, count);
    if (readDword(sr2::kCutscene)) writeCompleted(completed);
}

partner_thunks::Binding bind(Thunk thunk) {
    switch (thunk) {
        case Thunk::CutsceneAllRemotes: return {reinterpret_cast<void*>(&cutsceneAllRemotesStub), &g_handlerOriginal};
        case Thunk::CutsceneSetsReset: return {reinterpret_cast<void*>(&cutsceneSetsResetStub), nullptr};
        case Thunk::CutsceneReadyConsumed: return {reinterpret_cast<void*>(&cutsceneReadyConsumedStub), nullptr};
        default: return {nullptr, nullptr};
    }
}

}  // namespace cutscene_sync
