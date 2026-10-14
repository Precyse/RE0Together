#pragma once
#include <cstdint>
#include <vector>

#include "code_patch.h"
#include "sr2_addresses.h"

// The code and data edits of milestone 8 as data (pure, unit tested): the transport's message table rows of join_req and
// join_acc point at the gates, and the two sends of those messages go through the thunk that appends the trailer.
namespace version_gate_patches {

constexpr uintptr_t kJoinReqHandlerSlot = 0xE8BF3C;  // join_req row (id 2), handler pointer
constexpr uintptr_t kJoinAccHandlerSlot = 0xE8BF54;  // join_acc row (id 3), handler pointer
constexpr uintptr_t kJoinReqHandler = 0x815BC0;      // cdecl(payload, ctx)
constexpr uintptr_t kJoinAccHandler = 0x8162E0;      // cdecl(payload, ctx)
constexpr uintptr_t kJoinDenyHandler = 0x8163F0;     // cdecl(payload, ctx), reads the reason at payload + 4
constexpr uintptr_t kJoinDenySender = 0x816F60;      // ECX = address block, push reason (the caller pops)
constexpr uintptr_t kSend = sr2::kSendMessage;       // cdecl(buffer, length, 0, 0), ESI = member
constexpr uintptr_t kJoinReqSend = 0x815B9D;         // the call of kSend in the join_req sender
constexpr uintptr_t kJoinAccSend = 0x8162BB;         // the call of kSend in the join_acc sender

// Where the edits go to.
struct Thunks {
    uintptr_t gateJoinReq;
    uintptr_t gateJoinAcc;
    uintptr_t sendWithTrailer;
};

inline std::vector<uint8_t> pointerBytes(uintptr_t pointer) {
    return {static_cast<uint8_t>(pointer), static_cast<uint8_t>(pointer >> 8), static_cast<uint8_t>(pointer >> 16),
            static_cast<uint8_t>(pointer >> 24)};
}

// All sites, checked together: two table pointers and two send calls are written, the entries of the functions the gates
// call are only checked (the trace hooks may take them over later; they are installed after this check).
inline std::vector<CodePatch> patches(const Thunks& thunks) {
    return {
        {"gate_join_req_pointer", kJoinReqHandlerSlot, pointerBytes(kJoinReqHandler), pointerBytes(thunks.gateJoinReq)},
        {"gate_join_acc_pointer", kJoinAccHandlerSlot, pointerBytes(kJoinAccHandler), pointerBytes(thunks.gateJoinAcc)},
        retargetedCall("send_join_req_trailer", kJoinReqSend, kSend, thunks.sendWithTrailer),
        retargetedCall("send_join_acc_trailer", kJoinAccSend, kSend, thunks.sendWithTrailer),
        {"join_deny_sender", kJoinDenySender, {0x81, 0xEC, 0x24, 0x05, 0x00, 0x00, 0xA1, 0x50}, {}},
        {"join_deny_handler", kJoinDenyHandler, {0x80, 0x3D, 0x56, 0x8B, 0x52, 0x02, 0x00, 0x8B}, {}},
        {"join_req_handler", kJoinReqHandler, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x81, 0xEC}, {}},
        {"join_acc_handler", kJoinAccHandler, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x81, 0xEC}, {}},
    };
}

}  // namespace version_gate_patches
