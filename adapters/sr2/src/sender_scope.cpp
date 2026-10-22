#include "sender_scope.h"

#include <mutex>

#include "guest_call.h"
#include "log.h"
#include "memory.h"
#include "partner_scope.h"
#include "remote_players.h"
#include "sender_scope_rules.h"
#include "sr2_addresses.h"
#include "thunk_frame.h"

namespace {

using partner_sites::Thunk;

constexpr size_t kBufferArg = 0;
constexpr size_t kContextArg = 1;
constexpr uintptr_t kPayloadLengthBack = 2;  // the u16 payload length sits just before the payload (3 byte header)

void* g_dispatchOriginal = nullptr;
void* g_inOrderOriginal = nullptr;
void* g_lookMorphOriginal = nullptr;
void* g_lookComponentsOriginal = nullptr;
bool g_trace = false;
std::mutex g_lookMutex;
sender_scope_rules::LookCache g_looks;
thread_local uintptr_t t_sender = 0;

// Names `member` as the current sender for the calling thread while a handler runs.
class SenderMark {
public:
    explicit SenderMark(uintptr_t member) : previous_(t_sender) { t_sender = member; }
    ~SenderMark() { t_sender = previous_; }
    SenderMark(const SenderMark&) = delete;
    SenderMark& operator=(const SenderMark&) = delete;

private:
    uintptr_t previous_;
};

const char* joinMessageName(uint8_t id) {
    switch (id) {
        case sr2::kMessageJoinReq: return "join_req";
        case sr2::kMessageJoinAcc: return "join_acc";
        case sr2::kMessageJoinDeny: return "join_deny";
        case sr2::kMessageJoinNewSession: return "join_new_session";
        default: return nullptr;
    }
}

// The member a message came from, as the dispatcher itself reads it: a member whose live byte is clear is forgotten.
uintptr_t senderOf(uintptr_t context) {
    const uintptr_t member = context ? readDword(context + sr2::kMessageContextMemberOffset) : 0;
    return member && readByte(member + sr2::kMemberLiveOffset) ? member : 0;
}

// Calls the dispatcher at `original` with the getters answering `member` when it is a remote one. Returns its eax.
uint32_t runScoped(const GuestFrame* frame, void* original, uintptr_t member) {
    const uintptr_t target = reinterpret_cast<uintptr_t>(original);
    const uint32_t* args = frame->args;
    partner_scope::Target scope;
    if (!partner_scope::targetOfSender(member, scope)) {
        return guest_call::call(target, frame->regs, {args[kBufferArg], args[kContextArg]});
    }
    if (g_trace && !scope.human) {
        logger::writeUnlessRepeated("trace: scope: sender member 0x%X has no human yet", static_cast<unsigned>(member));
    }
    partner_scope::Override scoped(scope);
    const SenderMark mark(member);
    return guest_call::call(target, frame->regs, {args[kBufferArg], args[kContextArg]});
}

// The join messages are the only ones logged: the others are far too frequent.
void traceJoinMessage(uintptr_t message, uintptr_t context) {
    const uint8_t id = readByte(message + sr2::kMessageIdOffset);
    const char* name = joinMessageName(id);
    if (!name) return;
    uint16_t length = 0;
    readMemory(message + sr2::kMessageLengthOffset, &length, sizeof(length));
    logger::write("trace: message %s (id %u) length %u, context 0x%X, mapped member 0x%X", name, id, length,
                  static_cast<unsigned>(context), static_cast<unsigned>(readDword(context + sr2::kMessageContextMemberOffset)));
}

// SS1: cdecl(message, context); the sender is in the context.
void __cdecl scopeDispatch(GuestFrame* frame) {
    const uint32_t* args = frame->args;
    if (g_trace) traceJoinMessage(args[kBufferArg], args[kContextArg]);
    frame->regs.eax = runScoped(frame, g_dispatchOriginal, senderOf(args[kContextArg]));
}

// SS2: eax = the message, edx = the sender member, two arguments the caller pops.
void __cdecl scopeInOrder(GuestFrame* frame) {
    frame->regs.eax = runScoped(frame, g_inOrderOriginal, frame->regs.edx);
}

// LK1, LK2: the look handlers (cdecl(payload, context)) resolve the human by the net id in the payload. The payload is
// made to name the sender's own human before the handler queues the look, which is applied later outside the sender scope.
void bindLookToHuman(const uint32_t* args, uintptr_t member, uintptr_t human, const char* handler) {
    uint16_t payloadId = 0;
    if (!readMemory(args[kBufferArg], &payloadId, sizeof(payloadId))) return;
    const uint16_t wanted = sender_scope_rules::lookNetId(payloadId, remote_players::netIdOf(human));
    if (wanted == payloadId) return;
    if (g_trace) {
        logger::write("trace: %s from member 0x%X named net id 0x%04X, the sender's human 0x%X has 0x%04X", handler,
                      static_cast<unsigned>(member), payloadId, static_cast<unsigned>(human), wanted);
    }
    *reinterpret_cast<uint16_t*>(args[kBufferArg]) = wanted;
}

// True when the sender's slot already has this look applied for this human, so the handler is not called again.
bool lookUnchanged(const uint32_t* args, uintptr_t member, uintptr_t human, sender_scope_rules::LookKind kind, const char* handler) {
    uint16_t length = 0;
    uint8_t payload[sender_scope_rules::kMaxLookPayload];
    if (!readMemory(args[kBufferArg] - kPayloadLengthBack, &length, sizeof(length)) || !sender_scope_rules::lookCacheable(length) ||
        !readMemory(args[kBufferArg], payload, length)) {
        return false;
    }
    const uint8_t slot = readByte(member + sr2::kMemberSlotOffset);
    const sender_scope_rules::LookKey key{readDword(member + sr2::kMemberSessionOffset), slot, kind};
    const sender_scope_rules::Look look{readDword(human + sr2::kObjectHandleOffset), sender_scope_rules::lookDigest(payload, length)};
    {
        std::lock_guard<std::mutex> lock(g_lookMutex);
        if (!g_looks.unchanged(key, look, remote_players::remoteHumanCount())) return false;
    }
    if (g_trace) logger::write("trace: looks: %s from slot %u unchanged, not applied again", handler, slot);
    return true;
}

// The look handler at `original` for the message in `frame`, unless it carries the look already applied.
void runLookHandler(GuestFrame* frame, void* original, sender_scope_rules::LookKind kind, const char* handler) {
    const uint32_t* args = frame->args;
    const uintptr_t member = senderOf(args[kContextArg]);
    const uintptr_t human = member ? remote_players::humanOfMember(member) : 0;
    if (human) {
        bindLookToHuman(args, member, human, handler);
        if (lookUnchanged(args, member, human, kind, handler)) return;
    }
    frame->regs.eax = guest_call::call(reinterpret_cast<uintptr_t>(original), frame->regs, {args[kBufferArg], args[kContextArg]});
}

void __cdecl lookMorph(GuestFrame* frame) {
    runLookHandler(frame, g_lookMorphOriginal, sender_scope_rules::LookKind::Morph, "player_morph");
}

void __cdecl lookComponents(GuestFrame* frame) {
    runLookHandler(frame, g_lookComponentsOriginal, sender_scope_rules::LookKind::Components, "player_components");
}

PARTNER_WRAP_STUB(scopeDispatchStub, scopeDispatch, 0)
PARTNER_WRAP_STUB(scopeInOrderStub, scopeInOrder, 0)
PARTNER_WRAP_STUB(lookMorphStub, lookMorph, 0)
PARTNER_WRAP_STUB(lookComponentsStub, lookComponents, 0)

}  // namespace

namespace sender_scope {

void setTrace(bool trace) { g_trace = trace; }

uintptr_t currentSender() { return t_sender; }

partner_thunks::Binding bind(Thunk thunk) {
    switch (thunk) {
        case Thunk::ScopeDispatch: return {reinterpret_cast<void*>(&scopeDispatchStub), &g_dispatchOriginal};
        case Thunk::ScopeInOrder: return {reinterpret_cast<void*>(&scopeInOrderStub), &g_inOrderOriginal};
        case Thunk::LookMorphFromSender: return {reinterpret_cast<void*>(&lookMorphStub), &g_lookMorphOriginal};
        case Thunk::LookComponentsFromSender: return {reinterpret_cast<void*>(&lookComponentsStub), &g_lookComponentsOriginal};
        default: return {nullptr, nullptr};
    }
}

}  // namespace sender_scope
