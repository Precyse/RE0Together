// Checks which message types the state_net guard refuses outside state mode (no game). Exit 0 when every check passes.
#include <cstdio>

#include "../src/state_net_rule.h"

namespace {

namespace rule = state_net_rule;

constexpr uint16_t kBelowBlock = 0x013f;
constexpr uint16_t kAboveBlock = 0x0180;
constexpr uint16_t kPlayerState = 0x0100;
constexpr uint16_t kPadFrame = 0x0101;
constexpr uint16_t kProfileSpecific = 0x0200;
int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

void testBlockEdges() {
    check(rule::kFirstStateType == 0x0140 && rule::kLastStateType == 0x017f, "the block is 0x0140..0x017f");
    check(rule::isStateType(rule::kFirstStateType), "the first id of the block is a state type");
    check(rule::isStateType(rule::kLastStateType), "the last id of the block is a state type");
    check(!rule::isStateType(kBelowBlock), "the id below the block is not a state type");
    check(!rule::isStateType(kAboveBlock), "the id above the block is not a state type");
}

void testEveryStateTypeIsRefusedInInputMode() {
    for (uint32_t type = rule::kFirstStateType; type <= rule::kLastStateType; ++type) {
        check(!rule::allowed(false, static_cast<uint16_t>(type)), "a state type is refused in input mode");
        check(rule::allowed(true, static_cast<uint16_t>(type)), "a state type is allowed in state mode");
    }
}

void testOtherTypesAreUntouched() {
    for (const uint16_t type : {kPlayerState, kPadFrame, kBelowBlock, kAboveBlock, kProfileSpecific}) {
        check(rule::allowed(false, type), "a non-state type passes in input mode");
        check(rule::allowed(true, type), "a non-state type passes in state mode");
    }
}

void testPeerModeMismatch() {
    check(rule::peerRunsOtherMode(false, rule::kFirstStateType) && rule::peerRunsOtherMode(false, rule::kLastStateType),
          "a state-block frame shows a state-mode peer to an input-mode machine");
    check(!rule::peerRunsOtherMode(false, kPadFrame) && !rule::peerRunsOtherMode(false, kPlayerState),
          "pad and player state frames are an input-mode machine's own kind");
    check(rule::peerRunsOtherMode(true, kPadFrame), "a PAD_FRAME shows an input-mode peer to a state-mode machine");
    check(!rule::peerRunsOtherMode(true, rule::kFirstStateType) && !rule::peerRunsOtherMode(true, kPlayerState) &&
              !rule::peerRunsOtherMode(true, kProfileSpecific),
          "state-block, player state and other frames are a state-mode machine's own kind");
}

}  // namespace

int main() {
    testBlockEdges();
    testEveryStateTypeIsRefusedInInputMode();
    testOtherTypesAreUntouched();
    testPeerModeMismatch();
    if (g_failures == 0) std::printf("state_net_rule_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
