#include "anim_stream_requests.h"

#include <windows.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

#include "activity_state.h"
#include "anim_stream_rules.h"
#include "anim_stream_state.h"
#include "guest_call.h"
#include "log.h"
#include "memory.h"
#include "rate_limit.h"
#include "sr2_addresses.h"
#include "thunk_frame.h"

namespace {

using anim_stream_rules::kNoSlot;
using anim_stream_rules::RequestKey;
using anim_stream_rules::RequestKind;
using partner_sites::Thunk;

constexpr size_t kPayloadArg = 0;
constexpr size_t kContextArg = 1;

// The stream request payloads (V: 0xA79215..0xA7921E, 0xA79365..0xA7936C, 0x944495, 0x9444B4).
constexpr uintptr_t kRequestIdOffset = 0;
constexpr uintptr_t kRequestCategoryOffset = 4;
constexpr uintptr_t kWorldRequestKeyOffset = 5;
constexpr size_t kWorldPayloadBytes = 9;
constexpr size_t kCharacterPayloadBytes = 5;

enum Message : size_t { WorldAdd, WorldRemove, CharacterAdd, CharacterRemove, MessageCount };

constexpr const char* kMessageNames[MessageCount] = {"strm_req", "strm_rem_req", "osd_char_strm_req", "osd_char_strm_rem_req"};

constexpr RequestKind kindOf(Message message) { return message == WorldAdd || message == WorldRemove ? RequestKind::World : RequestKind::Character; }
constexpr bool isAdd(Message message) { return message == WorldAdd || message == CharacterAdd; }
constexpr Message removeOf(RequestKind kind) { return kind == RequestKind::World ? WorldRemove : CharacterRemove; }

void* g_requestOriginals[MessageCount] = {};
void* g_setChangeOriginal = nullptr;
void* g_resetOriginal = nullptr;
bool g_trace = false;
std::mutex g_mutex;
anim_stream_rules::RequestLedger g_ledger;
anim_stream_rules::SenderGates g_gates;
std::array<std::array<rate_limit::RateLimit, MessageCount>, anim_stream_rules::RequestLedger::kSlots> g_lineLimits;
std::array<rate_limit::RateLimit, anim_stream_rules::RequestLedger::kSlots> g_fullLimits;

// ---- the ledger ----

RequestKey keyOf(Message message, uintptr_t payload) {
    const RequestKind kind = kindOf(message);
    return {kind, readDword(payload + kRequestIdOffset), static_cast<int8_t>(readByte(payload + kRequestCategoryOffset)),
            kind == RequestKind::World ? readDword(payload + kWorldRequestKeyOffset) : 0};
}

// The payload a member sent for `key`: what the game's remove handler reads.
size_t payloadOf(const RequestKey& key, uint8_t (&out)[kWorldPayloadBytes]) {
    std::memcpy(out + kRequestIdOffset, &key.id, sizeof(key.id));
    out[kRequestCategoryOffset] = static_cast<uint8_t>(key.category);
    if (key.kind == RequestKind::Character) return kCharacterPayloadBytes;
    std::memcpy(out + kWorldRequestKeyOffset, &key.key, sizeof(key.key));
    return kWorldPayloadBytes;
}

void logRequest(uint8_t slot, Message message, const RequestKey& key, uint32_t held) {
    uint32_t triggers = 0;
    if (!g_lineLimits[slot][message].allow(GetTickCount64(), triggers)) return;
    char keyText[24] = "";
    if (key.kind == RequestKind::World) std::snprintf(keyText, sizeof(keyText), " key 0x%08X", key.key);
    logger::write("trace: stream requests: slot %u %s id 0x%08X category %d%s, slot holds %u", slot, kMessageNames[message], key.id, key.category,
                  keyText, held);
}

void noteRequest(uint8_t slot, Message message, uintptr_t payload) {
    if (slot >= anim_stream_rules::RequestLedger::kSlots) return;
    const RequestKey key = keyOf(message, payload);
    std::lock_guard<std::mutex> lock(g_mutex);
    g_ledger.reset(readDword(sr2::kSessionPointer));
    if (isAdd(message)) {
        uint32_t triggers = 0;
        if (!g_ledger.add(slot, key) && g_trace && g_fullLimits[slot].allow(GetTickCount64(), triggers)) {
            logger::write("trace: stream requests: slot %u ledger full, key not recorded", slot);
        }
    } else {
        g_ledger.remove(slot, key);
    }
    if (g_trace) logRequest(slot, message, key, g_ledger.held(slot));
}

// SR1 to SR4: cdecl(payload, context). The request is counted for its sender, then the game's handler runs as it did.
void record(GuestFrame* frame, Message message) {
    const uintptr_t payload = frame->args[kPayloadArg];
    const uintptr_t context = frame->args[kContextArg];
    const uint8_t slot = anim_stream_state::slotOfMember(activity_state::senderOf(context));
    if (slot != kNoSlot) noteRequest(slot, message, payload);
    frame->regs.eax = guest_call::call(reinterpret_cast<uintptr_t>(g_requestOriginals[message]), frame->regs,
                                       {static_cast<uint32_t>(payload), static_cast<uint32_t>(context)});
}

void __cdecl ledgerWorldAdd(GuestFrame* frame) { record(frame, WorldAdd); }
void __cdecl ledgerWorldRemove(GuestFrame* frame) { record(frame, WorldRemove); }
void __cdecl ledgerCharacterAdd(GuestFrame* frame) { record(frame, CharacterAdd); }
void __cdecl ledgerCharacterRemove(GuestFrame* frame) { record(frame, CharacterRemove); }

// ---- the set change gates ----

anim_stream_rules::SenderGates::Bank readGates() {
    anim_stream_rules::SenderGates::Bank bank;
    for (size_t i = 0; i < bank.size(); ++i) bank[i] = dwordField(sr2::kSetChangeGates, i * sizeof(uint32_t));
    return bank;
}

void writeGates(const anim_stream_rules::SenderGates::Bank& bank) {
    for (size_t i = 0; i < bank.size(); ++i) dwordField(sr2::kSetChangeGates, i * sizeof(uint32_t)) = bank[i];
}

// Remembers what the loads of the handler wrote, and says which of them the shared gate of the shipped game would have skipped.
void noteLoads(uint8_t slot, uint32_t time, const anim_stream_rules::SenderGates::Bank& before,
               const anim_stream_rules::SenderGates::Bank& after) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_gates.store(slot, after);
    for (size_t index = 0; index < after.size(); ++index) {
        if (after[index] == before[index]) continue;
        if (g_trace && g_gates.sharedWouldSkip(index, time)) {
            logger::write("trace: set changes: slot %u time %u index %zu loads, the shared gate (time %u from slot %u) would have skipped it",
                          slot, time, index, g_gates.sharedTime(index), g_gates.sharedSlot(index));
        }
        g_gates.noteLoad(index, time, slot);
    }
}

// SC1: cdecl(payload, context). The handler runs with the sender's own gates in the game's, and leaves them in the sender's bank.
void __cdecl setChangePerSender(GuestFrame* frame) {
    const uintptr_t payload = frame->args[kPayloadArg];
    const uintptr_t context = frame->args[kContextArg];
    const uintptr_t member = activity_state::senderOf(context);
    const uint8_t slot = anim_stream_state::slotOfMember(member);
    const auto runHandler = [&] {
        frame->regs.eax = guest_call::call(reinterpret_cast<uintptr_t>(g_setChangeOriginal), frame->regs,
                                           {static_cast<uint32_t>(payload), static_cast<uint32_t>(context)});
    };
    if (slot >= anim_stream_rules::SenderGates::kSlots) {
        runHandler();
        return;
    }
    anim_stream_rules::SenderGates::Bank before;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        before = g_gates.load(slot, member);
    }
    writeGates(before);
    runHandler();
    noteLoads(slot, readDword(payload), before, readGates());
}

// SC2: the game reset both gates; every bank is reset with them.
void __cdecl resetSenderGates(GuestFrame* frame) {
    frame->regs.eax = guest_call::call(reinterpret_cast<uintptr_t>(g_resetOriginal), frame->regs);
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_gates.resetAll();
    }
    if (g_trace) logger::write("trace: set changes: gates reset");
}

PARTNER_WRAP_STUB(ledgerWorldAddStub, ledgerWorldAdd, 0)
PARTNER_WRAP_STUB(ledgerWorldRemoveStub, ledgerWorldRemove, 0)
PARTNER_WRAP_STUB(ledgerCharacterAddStub, ledgerCharacterAdd, 0)
PARTNER_WRAP_STUB(ledgerCharacterRemoveStub, ledgerCharacterRemove, 0)
PARTNER_WRAP_STUB(setChangePerSenderStub, setChangePerSender, 0)
PARTNER_WRAP_STUB(resetSenderGatesStub, resetSenderGates, 0)

}  // namespace

namespace anim_stream_requests {

void setTrace(bool trace) { g_trace = trace; }

Released releaseLeaver(uintptr_t member) {
    Released released{0, 0};
    const uint8_t slot = anim_stream_state::slotOfMember(member);
    std::vector<anim_stream_rules::HeldRequest> held;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_ledger.reset(readDword(sr2::kSessionPointer));
        held = g_ledger.take(slot);
    }
    const uint32_t context[2] = {0, static_cast<uint32_t>(member)};  // [context+4] = the member the message came from
    for (const anim_stream_rules::HeldRequest& request : held) {
        const void* handler = g_requestOriginals[removeOf(request.key.kind)];
        if (!handler) continue;
        uint8_t payload[kWorldPayloadBytes];
        payloadOf(request.key, payload);
        for (uint32_t i = 0; i < request.count; ++i) {
            guest_call::call(reinterpret_cast<uintptr_t>(handler), {}, {static_cast<uint32_t>(reinterpret_cast<uintptr_t>(payload)),
                                                                          static_cast<uint32_t>(reinterpret_cast<uintptr_t>(context))});
        }
        (request.key.kind == RequestKind::World ? released.world : released.character) += request.count;
    }
    return released;
}

partner_thunks::Binding bind(Thunk thunk) {
    switch (thunk) {
        case Thunk::LedgerWorldAdd: return {reinterpret_cast<void*>(&ledgerWorldAddStub), &g_requestOriginals[WorldAdd]};
        case Thunk::LedgerWorldRemove: return {reinterpret_cast<void*>(&ledgerWorldRemoveStub), &g_requestOriginals[WorldRemove]};
        case Thunk::LedgerCharacterAdd: return {reinterpret_cast<void*>(&ledgerCharacterAddStub), &g_requestOriginals[CharacterAdd]};
        case Thunk::LedgerCharacterRemove: return {reinterpret_cast<void*>(&ledgerCharacterRemoveStub), &g_requestOriginals[CharacterRemove]};
        case Thunk::SetChangePerSender: return {reinterpret_cast<void*>(&setChangePerSenderStub), &g_setChangeOriginal};
        case Thunk::ResetSenderGates: return {reinterpret_cast<void*>(&resetSenderGatesStub), &g_resetOriginal};
        default: return {nullptr, nullptr};
    }
}

}  // namespace anim_stream_requests
