#include "cutscene_sync.h"

#include <cstdio>
#include <mutex>
#include <string>

#include "activity_state.h"
#include "cutscene_sync_rules.h"
#include "fpu_guard.h"
#include "guest_call.h"
#include "log.h"
#include "memory.h"
#include "script_trace.h"
#include "script_trace_rules.h"
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
bool g_trace = false;

uint8_t& byteAt(uintptr_t address) { return *reinterpret_cast<uint8_t*>(address); }

bool skippedTogether() {
    const uintptr_t cutscene = readDword(sr2::kCutscene);
    return cutscene && byteAt(cutscene + sr2::kCutsceneSharedSkipOffset) != 0;
}

struct HandshakeBytes {
    uint8_t ready, skipped, done;  // D1D, D1F, D20
};

HandshakeBytes handshakeBytes() {
    return {byteAt(sr2::kCutsceneRemoteReady), byteAt(sr2::kCutsceneRemoteSkipped), byteAt(sr2::kCutsceneRemoteDone)};
}

uint8_t slotOf(uintptr_t member) { return member ? readByte(member + sr2::kMemberSlotOffset) : 0; }

std::string slotsOf(const uintptr_t* members, size_t count) {
    uint8_t slots[cutscene_sync_rules::kMaxMembers] = {};
    for (size_t i = 0; i < count && i < cutscene_sync_rules::kMaxMembers; ++i) slots[i] = slotOf(members[i]);
    return script_trace_rules::slotListText(slots, count);
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

std::string handshakeBytesText(const HandshakeBytes& bytes) {
    char text[16];
    std::snprintf(text, sizeof(text), "%u/%u/%u", bytes.ready, bytes.skipped, bytes.done);
    return text;
}

// The message that passed through: sub 0, 4, 5, or a skip of a cutscene skipped together.
void tracePassed(int8_t sub, uintptr_t sender, const HandshakeBytes& before) {
    logger::write("trace: cutscene: sub %d from slot %u passed, D1D/D1F/D20 %s -> %s", sub, slotOf(sender),
                  handshakeBytesText(before).c_str(), handshakeBytesText(handshakeBytes()).c_str());
}

// The message of a tracked handshake, with the sender set and the members it waits for. Written after the bytes were put back.
void traceHandshake(int8_t sub, Handshake handshake, uintptr_t sender, const uintptr_t* senders, size_t senderCount,
                    const uintptr_t* required, size_t requiredCount, bool complete, const HandshakeBytes& before) {
    logger::write("trace: cutscene: sub %d (%s) from slot %u, set %s, required %s, %s, D1D/D1F/D20 %s -> %s", sub,
                  cutscene_sync_rules::name(handshake), slotOf(sender), slotsOf(senders, senderCount).c_str(),
                  slotsOf(required, requiredCount).c_str(), complete ? "released" : "withheld", handshakeBytesText(before).c_str(),
                  handshakeBytesText(handshakeBytes()).c_str());
}

// MC1: cdecl(payload, context), the payload starts with the sub id.
void __cdecl cutsceneAllRemotes(GuestFrame* frame) {
    const uint32_t* args = frame->args;
    const int8_t sub = static_cast<int8_t>(byteAt(args[kPayloadArg]));
    const Handshake handshake = cutscene_sync_rules::handshakeOf(sub, skippedTogether());
    const uintptr_t sender = activity_state::senderOf(args[kContextArg]);
    const HandshakeBytes before = handshakeBytes();
    if (handshake == Handshake::None) {
        frame->regs.eax = runHandler(frame, cutscene_sync_rules::isStart(sub));
        if (g_trace) tracePassed(sub, sender, before);
        return;
    }
    const Bytes bytes = bytesOf(handshake);
    const uint8_t firstBefore = *bytes.first;
    const uint8_t secondBefore = bytes.second ? *bytes.second : 0;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_senders.add(handshake, sender);
    }
    frame->regs.eax = runHandler(frame, false);
    uintptr_t required[cutscene_sync_rules::kMaxMembers];
    uintptr_t senders[cutscene_sync_rules::kMaxMembers];
    const size_t count = activity_state::remoteMembersInGame(required);
    bool complete = false;
    size_t senderCount = 0;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        complete = g_senders.complete(handshake, required, count);
        g_senders.setWithheld(handshake, !complete);
        senderCount = g_senders.senders(handshake, senders);
    }
    if (!complete) {
        *bytes.first = firstBefore;
        if (bytes.second) *bytes.second = secondBefore;
    }
    if (g_trace) traceHandshake(sub, handshake, sender, senders, senderCount, required, count, complete, before);
}

PARTNER_WRAP_STUB(cutsceneAllRemotesStub, cutsceneAllRemotes, 0)

// MC2: the game's own `mov byte [0x2527D1E], 0` (this machine has not asked to skip), and the sets forgotten.
void __cdecl startCutscene() {
    byteAt(sr2::kCutsceneLocalSkipped) = 0;
    const bool received = t_insideStart;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_senders.started(received);
    }
    if (g_trace) {
        logger::write("trace: cutscene: start, cutscene 0x%X (%s), ready set %s, D1D/D1F/D20 %s", readDword(sr2::kCutscene),
                      received ? "host's start received" : "own start", received ? "kept" : "cleared",
                      handshakeBytesText(handshakeBytes()).c_str());
    }
}

// The game's own `mov byte [0x2527D1D], 0` of CS1 and CS2, and the ready set forgotten with the byte.
void clearRemoteReady() {
    byteAt(sr2::kCutsceneRemoteReady) = 0;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_senders.clearReady();
}

// CS1: the playback started.
void __cdecl consumeRemoteReady() {
    clearRemoteReady();
    if (g_trace) logger::write("trace: cutscene: playback started, ready consumed");
}

// CS2: the wait for the remotes gave up.
void __cdecl giveUpReadyWait() {
    clearRemoteReady();
    if (g_trace) logger::write("trace: cutscene: ready wait gave up");
}

// CE3: the game's own `mov byte [0x2527D15], 1` at the end of a cutscene in a session: the done wait starts, which the game's
// timer ends after 15 s. The local done was sent, or marked pending in D21.
void __cdecl startDoneWait() {
    byteAt(sr2::kCutsceneDoneWait) = 1;
    const uint8_t pending = byteAt(sr2::kCutsceneDonePending);
    logger::write("trace: cutscene: end, done wait starts (%u s), local done %s (D21 %u), D1D/D1F/D20 %s", sr2::kCutsceneDoneWaitMs / 1000,
                  pending ? "pending" : "sent", pending, handshakeBytesText(handshakeBytes()).c_str());
}

// CE4: the game's own `mov byte [0x2527D20], 1` of the cutscene tick while the done wait runs: its timer ran out, or there is
// no session. It does not go through the handler, so MC1 cannot withhold it.
void __cdecl endDoneWaitByTimer() {
    byteAt(sr2::kCutsceneRemoteDone) = 1;
    logger::write("trace: cutscene: done wait ended by the game's timer or no session, D1D/D1F/D20 %s",
                  handshakeBytesText(handshakeBytes()).c_str());
}

// CE5: the game's own `mov byte [0x2527D15], 0`: the done wait is over and the cutscene fades out of its black.
void __cdecl finishCutscene() {
    byteAt(sr2::kCutsceneDoneWait) = 0;
    FpuGuard guard;
    const script_trace_rules::FadeSnapshot fade = script_trace::readFade();
    logger::write("trace: cutscene: finished, fade in after E8DD1F=%u, D1D/D1F/D20 %s, fade state %u percent %.3f",
                  byteAt(sr2::kCutsceneFadeInAfter), handshakeBytesText(handshakeBytes()).c_str(), fade.state, fade.percent);
}

// Replace those stores. Registers and flags are kept, and the x87 stack is not touched (a value waits on it).
PARTNER_BLOCK_STUB(cutsceneSetsResetStub, startCutscene)
PARTNER_BLOCK_STUB(cutsceneReadyConsumedStub, consumeRemoteReady)
PARTNER_BLOCK_STUB(cutsceneReadyGaveUpStub, giveUpReadyWait)
PARTNER_BLOCK_STUB(cutsceneDoneWaitStub, startDoneWait)
PARTNER_BLOCK_STUB(cutsceneDoneTimeoutStub, endDoneWaitByTimer)
PARTNER_BLOCK_STUB(cutsceneFinishedStub, finishCutscene)

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

void setTrace(bool trace) { g_trace = trace; }

partner_thunks::Binding bind(Thunk thunk) {
    switch (thunk) {
        case Thunk::CutsceneAllRemotes: return {reinterpret_cast<void*>(&cutsceneAllRemotesStub), &g_handlerOriginal};
        case Thunk::CutsceneSetsReset: return {reinterpret_cast<void*>(&cutsceneSetsResetStub), nullptr};
        case Thunk::CutsceneReadyConsumed: return {reinterpret_cast<void*>(&cutsceneReadyConsumedStub), nullptr};
        case Thunk::CutsceneReadyGaveUp: return {reinterpret_cast<void*>(&cutsceneReadyGaveUpStub), nullptr};
        case Thunk::CutsceneDoneWait: return {reinterpret_cast<void*>(&cutsceneDoneWaitStub), nullptr};
        case Thunk::CutsceneDoneTimeout: return {reinterpret_cast<void*>(&cutsceneDoneTimeoutStub), nullptr};
        case Thunk::CutsceneFinished: return {reinterpret_cast<void*>(&cutsceneFinishedStub), nullptr};
        default: return {nullptr, nullptr};
    }
}

}  // namespace cutscene_sync
