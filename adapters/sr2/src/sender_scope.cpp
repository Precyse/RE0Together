#include "sender_scope.h"

#include "guest_call.h"
#include "log.h"
#include "memory.h"
#include "partner_scope.h"
#include "sr2_addresses.h"
#include "thunk_frame.h"

namespace {

using partner_sites::Thunk;

constexpr size_t kBufferArg = 0;
constexpr size_t kContextArg = 1;

void* g_dispatchOriginal = nullptr;
void* g_inOrderOriginal = nullptr;
bool g_trace = false;

uint8_t byteAt(uintptr_t address) {
    uint8_t value = 0;
    readMemory(address, &value, sizeof(value));
    return value;
}

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
    return member && byteAt(member + sr2::kMemberLiveOffset) ? member : 0;
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
    return guest_call::call(target, frame->regs, {args[kBufferArg], args[kContextArg]});
}

// The join messages are the only ones logged: the others are far too frequent.
void traceJoinMessage(uintptr_t message, uintptr_t context) {
    const uint8_t id = byteAt(message + sr2::kMessageIdOffset);
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

PARTNER_WRAP_STUB(scopeDispatchStub, scopeDispatch, 0)
PARTNER_WRAP_STUB(scopeInOrderStub, scopeInOrder, 0)

}  // namespace

namespace sender_scope {

void setTrace(bool trace) { g_trace = trace; }

partner_thunks::Binding bind(Thunk thunk) {
    switch (thunk) {
        case Thunk::ScopeDispatch: return {reinterpret_cast<void*>(&scopeDispatchStub), &g_dispatchOriginal};
        case Thunk::ScopeInOrder: return {reinterpret_cast<void*>(&scopeInOrderStub), &g_inOrderOriginal};
        default: return {nullptr, nullptr};
    }
}

}  // namespace sender_scope
