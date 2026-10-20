#include "pause_mask.h"

#include <windows.h>

#include "guest_call.h"
#include "log.h"
#include "memory.h"
#include "pause_mask_rules.h"
#include "rate_limit.h"
#include "sr2_addresses.h"
#include "thunk_frame.h"

namespace {

using partner_sites::Thunk;

constexpr size_t kContextArg = 1;
constexpr uint32_t kPausedFlagBits = 0xFF;  // cl, set by the handler's `setne cl`

bool g_trace = false;
rate_limit::RateLimit g_noSenderLimit;

uint8_t slotOf(uintptr_t member) { return member ? readByte(member + sr2::kMemberSlotOffset) : 0; }

uint8_t& pauseByte() { return *reinterpret_cast<uint8_t*>(sr2::kPartnerPaused); }

struct PauseState {
    uint32_t count;
    uint8_t worldPaused;
    uint8_t remotePause;
};

PauseState readPauseState() {
    return {readDword(sr2::kPauseRefCount), readByte(sr2::kWorldPausedFlag), readByte(sr2::kRemotePauseFlag)};
}

// The game's apply for the byte as it is now, with the pause count before and after in the trace.
void applyPartnerPause(uint8_t mask) {
    const PauseState before = readPauseState();
    guest_call::call(sr2::kApplyPartnerPause, guest_call::Regs{});
    if (!g_trace) return;
    const PauseState after = readPauseState();
    logger::write("trace: pause: apply 0x8CA190 with mask 0x%02X: count %u -> %u, world paused %u -> %u, remote pause %u -> %u", mask,
                  before.count, after.count, before.worldPaused, after.worldPaused, before.remotePause, after.remotePause);
}

const char* outcomeOf(const pause_mask_rules::Change& change) { return change.apply ? "apply" : "no change"; }

// Stores the change in the byte and applies it when the rule says so.
void commit(const pause_mask_rules::Change& change) {
    pauseByte() = change.after;
    if (change.apply) applyPartnerPause(change.after);
}

// N4a: the 0x80 handler's cdecl(payload, context), entered by a jump where it stored the flag (cl). eax is the local member.
void __cdecl onPauseMessage(GuestFrame* frame) {
    const uintptr_t sender = readDword(frame->args[kContextArg] + sr2::kMessageContextMemberOffset);
    if (!sender) {
        uint32_t messages = 0;
        if (g_noSenderLimit.allow(GetTickCount64(), messages)) {
            logger::write("pause: message 0x80 without a sender member, ignored (%u since the last line)", messages);
        }
        return;
    }
    const uint32_t localState = frame->regs.eax ? readDword(frame->regs.eax + sr2::kMemberStateOffset) : 0;
    const bool paused = (frame->regs.ecx & kPausedFlagBits) != 0;
    const uint8_t slot = slotOf(sender);
    const bool inGame = localState >= sr2::kMemberStateInGame;
    const pause_mask_rules::Change change = pause_mask_rules::afterMessage(pauseByte(), slot, paused, inGame);
    if (g_trace) {
        logger::write("trace: pause: slot %u sent %u, mask 0x%02X -> 0x%02X, local state %u, %s", slot, paused ? 1u : 0u, change.before,
                      change.after, localState, outcomeOf(change));
    }
    commit(change);
}

PARTNER_WRAP_STUB(pauseMessageStub, onPauseMessage, 0)

}  // namespace

namespace pause_mask {

void afterLeave(uintptr_t leaver) {
    const uint8_t slot = slotOf(leaver);
    const pause_mask_rules::Change change = pause_mask_rules::afterLeave(pauseByte(), slot);
    if (g_trace) {
        logger::write("trace: pause: slot %u left, mask 0x%02X -> 0x%02X, %s", slot, change.before, change.after, outcomeOf(change));
    }
    commit(change);
}

void setTrace(bool trace) { g_trace = trace; }

partner_thunks::Binding bind(Thunk thunk) {
    switch (thunk) {
        case Thunk::PauseMessage: return {reinterpret_cast<void*>(&pauseMessageStub), nullptr};
        default: return {nullptr, nullptr};
    }
}

}  // namespace pause_mask
