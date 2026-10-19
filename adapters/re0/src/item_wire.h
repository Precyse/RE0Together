#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>

#include "item_ledger_rule.h"
#include "protocol.h"

// Wire format of the host item ledger (partner_mode=state, docs/RE0_NOTES.md "State mode: item ledger"). Pure, no game
// access, unit tested.
namespace item_wire {

constexpr uint16_t kMsgTakeRequest = proto::kFirstGameType + 0x70;  // 0x0170, guest to host, reliable
constexpr uint16_t kMsgTakeResult = proto::kFirstGameType + 0x71;   // 0x0171, host to all, reliable

constexpr uint32_t kMaxItemId = 0xff;     // item ids the game uses are below this
constexpr uint32_t kMaxItemCount = 0xffff;  // the game keeps a stack count in 16 bits

// TAKE_REQUEST: the taker's pickup reached its take phase. `seq` pairs the result with the request; the position is the
// floor item's, read where the taker stands.
struct TakeRequest {
    uint16_t seq;
    uint16_t scene;
    uint32_t itemId;
    uint32_t count;
    item_ledger_rule::Vec3 pos;
};
static_assert(sizeof(TakeRequest) == 24);

// TAKE_RESULT: the host's answer, to everyone; only `requester` acts on it. `serial` numbers the ledger entry.
struct TakeResult {
    uint32_t serial;
    uint16_t seq;
    uint16_t scene;
    uint8_t requester;
    uint8_t answer;  // item_ledger_rule::Answer, Granted..Claimed
    uint16_t reserved;
};
static_assert(sizeof(TakeResult) == 12);

inline item_ledger_rule::Key keyOf(const TakeRequest& request) { return {request.scene, request.itemId, request.pos}; }

// A received TAKE_REQUEST of the right size with an item the game has and a finite position, or nothing.
inline std::optional<TakeRequest> decodeRequest(std::span<const uint8_t> bytes) {
    TakeRequest request{};
    if (bytes.size() != sizeof(request)) return std::nullopt;
    std::memcpy(&request, bytes.data(), sizeof(request));
    if (request.itemId > kMaxItemId || request.count > kMaxItemCount) return std::nullopt;
    for (const float coordinate : request.pos) {
        if (!std::isfinite(coordinate)) return std::nullopt;
    }
    return request;
}

// A received TAKE_RESULT of the right size with an answer the host can give, or nothing.
inline std::optional<TakeResult> decodeResult(std::span<const uint8_t> bytes) {
    TakeResult result{};
    if (bytes.size() != sizeof(result)) return std::nullopt;
    std::memcpy(&result, bytes.data(), sizeof(result));
    if (result.answer > item_ledger_rule::kLastWireAnswer) return std::nullopt;
    return result;
}

}  // namespace item_wire
