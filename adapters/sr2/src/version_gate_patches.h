#pragma once
#include <cstdint>
#include <vector>

#include "code_patch.h"
#include "sr2_addresses.h"

// The code and data edits of milestones 8 and 11 B3 as data (pure, unit tested): the transport's message table rows of
// join_req, join_acc and join_deny point at the gates and the deny note, the two sends of join_req and join_acc go through
// the thunk that appends the trailer, and the one call that turns a join error code into text goes through the thunk that
// shows the named reason.
namespace version_gate_patches {

constexpr uintptr_t kJoinReqHandlerSlot = 0xE8BF3C;  // join_req row (id 2), handler pointer
constexpr uintptr_t kJoinAccHandlerSlot = 0xE8BF54;  // join_acc row (id 3), handler pointer
constexpr uintptr_t kJoinDenyHandlerSlot = 0xE8BF6C; // join_deny row, handler pointer
constexpr uintptr_t kJoinAccHandler = 0x8162E0;      // cdecl(payload, ctx)
constexpr uintptr_t kJoinDenyHandler = 0x8163F0;     // cdecl(payload, ctx), reads the reason at payload + 4
constexpr uintptr_t kJoinDenySender = 0x816F60;      // ECX = address block, push reason (the caller pops): the game's own join_deny
constexpr uintptr_t kJoinDenyHeaderFlag = 0xE8BF7C;  // byte of the join_deny row: 0 = the 3 byte header the adapter builds
constexpr uintptr_t kSendToAddress = 0x814790;       // ECX = message, push address block {ip, port, 0, 0, 0}, push length (the caller pops 8)
constexpr uintptr_t kErrorText = 0x81EA50;           // ECX = error code, returns the wide text; code 8 = MULTI_ERROR_JOIN_REJECTED
constexpr uintptr_t kErrorTextCall = 0x81ECE7;       // VG1: the call of kErrorText in the error popup
constexpr uint32_t kJoinRejectedCode = 8;
constexpr uintptr_t kJoinReqSend = 0x815B9D;         // the call of sr2::kSendToMember in the join_req sender
constexpr uintptr_t kJoinAccSend = 0x8162BB;         // the call of sr2::kSendToMember in the join_acc sender

// Where the edits go to.
struct Thunks {
    uintptr_t gateJoinReq;
    uintptr_t gateJoinAcc;
    uintptr_t sendWithTrailer;
    uintptr_t noteJoinDeny;
    uintptr_t refusalText;
};

// All sites, checked together: three table pointers and three calls are written, the entries of the functions the gates
// call are only checked (the trace hooks may take them over later; they are installed after this check).
inline std::vector<CodePatch> patches(const Thunks& thunks) {
    return {
        {"gate_join_req_pointer", kJoinReqHandlerSlot, littleEndian(sr2::kJoinReqHandler), littleEndian(thunks.gateJoinReq)},
        {"gate_join_acc_pointer", kJoinAccHandlerSlot, littleEndian(kJoinAccHandler), littleEndian(thunks.gateJoinAcc)},
        {"note_join_deny_pointer", kJoinDenyHandlerSlot, littleEndian(kJoinDenyHandler), littleEndian(thunks.noteJoinDeny)},
        retargetedCall("send_join_req_trailer", kJoinReqSend, sr2::kSendToMember, thunks.sendWithTrailer),
        retargetedCall("send_join_acc_trailer", kJoinAccSend, sr2::kSendToMember, thunks.sendWithTrailer),
        retargetedCall("error_text_refusal", kErrorTextCall, kErrorText, thunks.refusalText),
        {"join_deny_sender", kJoinDenySender, {0x81, 0xEC, 0x24, 0x05, 0x00, 0x00, 0xA1, 0x50}, {}},
        {"send_to_address", kSendToAddress, {0x81, 0xEC, 0x10, 0x02, 0x00, 0x00, 0xA1, 0x50}, {}},
        {"error_text", kErrorText, {0x83, 0xC1, 0xFF, 0x33, 0xC0, 0x83, 0xF9, 0x0C}, {}},
        {"join_deny_handler", kJoinDenyHandler, {0x80, 0x3D, 0x56, 0x8B, 0x52, 0x02, 0x00, 0x8B}, {}},
        {"join_req_handler", sr2::kJoinReqHandler, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x81, 0xEC}, {}},
        {"join_acc_handler", kJoinAccHandler, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x81, 0xEC}, {}},
    };
}

}  // namespace version_gate_patches
