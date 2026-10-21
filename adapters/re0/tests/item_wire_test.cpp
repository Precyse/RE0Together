// Checks the item ledger wire format (no game). Exit 0 when every check passes.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

#include "../src/item_wire.h"

namespace {

namespace wire = item_wire;
using item_ledger_rule::Answer;

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

template <typename T>
std::vector<uint8_t> bytesOf(const T& value) {
    std::vector<uint8_t> out(sizeof(T));
    std::memcpy(out.data(), &value, sizeof(T));
    return out;
}

constexpr uint16_t kEpoch = 5;

wire::TakeRequest sampleRequest() { return {7, 0x0a05, 0x2b, 1, {100.0f, 0.0f, 200.0f}, kEpoch, 0}; }

void testIds() {
    check(wire::kMsgTakeRequest == 0x0170, "TAKE_REQUEST is 0x0170");
    check(wire::kMsgTakeResult == 0x0171, "TAKE_RESULT is 0x0171");
}

void testRequestRoundTrip() {
    const wire::TakeRequest sent = sampleRequest();
    const auto got = wire::decodeRequest(bytesOf(sent));
    check(got.has_value(), "a good request decodes");
    if (!got) return;
    check(got->seq == 7 && got->scene == 0x0a05 && got->itemId == 0x2b && got->count == 1, "request fields survive");
    check(got->pos == sent.pos, "request position survives");
    check(got->worldEpoch == kEpoch, "the requester's world epoch survives");
    const item_ledger_rule::Key key = wire::keyOf(*got);
    check(key.scene == 0x0a05 && key.itemId == 0x2b && key.pos == sent.pos, "the ledger key is the request's item");
}

void testRequestRefusals() {
    std::vector<uint8_t> bytes = bytesOf(sampleRequest());
    check(!wire::decodeRequest(std::vector<uint8_t>(bytes.begin(), bytes.end() - 1)), "a short request is refused");
    bytes.push_back(0);
    check(!wire::decodeRequest(bytes), "a long request is refused");

    wire::TakeRequest request = sampleRequest();
    request.itemId = wire::kMaxItemId + 1;
    check(!wire::decodeRequest(bytesOf(request)), "an item id the game does not have is refused");
    request = sampleRequest();
    request.itemId = wire::kMaxItemId;
    check(wire::decodeRequest(bytesOf(request)).has_value(), "the highest item id is accepted");
    request.count = wire::kMaxItemCount + 1;
    check(!wire::decodeRequest(bytesOf(request)), "a count past 16 bits is refused");
    request = sampleRequest();
    request.pos[1] = std::numeric_limits<float>::quiet_NaN();
    check(!wire::decodeRequest(bytesOf(request)), "a NaN position is refused");
    request.pos[1] = std::numeric_limits<float>::infinity();
    check(!wire::decodeRequest(bytesOf(request)), "an infinite position is refused");
}

void testResultRoundTrip() {
    const wire::TakeResult sent{42, 7, 0x0a05, 1, static_cast<uint8_t>(Answer::Claimed), 0};
    const auto got = wire::decodeResult(bytesOf(sent));
    check(got.has_value(), "a good result decodes");
    if (!got) return;
    check(got->serial == 42 && got->seq == 7 && got->scene == 0x0a05 && got->requester == 1, "result fields survive");
    check(static_cast<Answer>(got->answer) == Answer::Claimed, "the answer survives");
}

void testResultRefusals() {
    wire::TakeResult result{1, 1, 1, 1, static_cast<uint8_t>(Answer::Granted), 0};
    check(wire::decodeResult(bytesOf(result)).has_value(), "Granted is a wire answer");
    result.answer = static_cast<uint8_t>(Answer::Taken);
    check(wire::decodeResult(bytesOf(result)).has_value(), "Taken is a wire answer");
    result.answer = static_cast<uint8_t>(Answer::Stale);
    check(wire::decodeResult(bytesOf(result)).has_value(), "Stale is a wire answer: the host refuses a request from before its load");
    result.answer = static_cast<uint8_t>(Answer::TimedOut);
    check(!wire::decodeResult(bytesOf(result)), "the local timeout never travels");
    check(!wire::decodeResult(std::vector<uint8_t>(3)), "a short result is refused");
}

}  // namespace

int main() {
    testIds();
    testRequestRoundTrip();
    testRequestRefusals();
    testResultRoundTrip();
    testResultRefusals();
    if (g_failures == 0) std::printf("item_wire_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
