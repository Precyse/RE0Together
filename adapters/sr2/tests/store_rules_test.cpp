// The pure rules of the store, garage and activity level menu sync (no game).
#include <cstdio>

#include "../src/sr2_addresses.h"
#include "../src/store_rules.h"
#include "check.h"

namespace {

using store_rules::Route;

constexpr uintptr_t kMember = 0x02C41234;
constexpr uint32_t kBothHiddenBits = 0x14;
constexpr uint32_t kSomeFlags = 0x00000081;
constexpr uint32_t kOneHiddenBit = 0x10;

void testLevelMenuRoute() {
    check(store_rules::levelMenuRoute(kMember) == Route::Member, "a known owner member gets the menu request alone");
    check(store_rules::levelMenuRoute(0) == Route::Broadcast, "an owner that is no remote member keeps the game's broadcast");
}

void testAnswerRoute() {
    check(store_rules::answerRoute(false, kMember) == Route::Member, "a guest answers to the host member alone");
    check(store_rules::answerRoute(true, kMember) == Route::Broadcast, "the host keeps the game's broadcast");
    check(store_rules::answerRoute(false, 0) == Route::Broadcast, "a guest without a known host member keeps the game's broadcast");
    check(store_rules::answerRoute(true, 0) == Route::Broadcast, "the host without a host member keeps the game's broadcast");
}

void testWithHidden() {
    check(sr2::kHumanHiddenFlags == kBothHiddenBits, "the hidden bits are the 0x10 and 0x04 the game's blocks set");
    check(store_rules::withHidden(0, true) == kBothHiddenBits, "hiding sets both bits");
    check(store_rules::withHidden(kBothHiddenBits, false) == 0, "showing clears both bits");
    check(store_rules::withHidden(kSomeFlags, true) == (kSomeFlags | kBothHiddenBits), "hiding keeps the other bits");
    check(store_rules::withHidden(kSomeFlags | kBothHiddenBits, false) == kSomeFlags, "showing keeps the other bits");
    check(store_rules::withHidden(kOneHiddenBit, true) == kBothHiddenBits, "hiding completes a half hidden human");
    check(store_rules::withHidden(store_rules::withHidden(kSomeFlags, true), true) == store_rules::withHidden(kSomeFlags, true),
          "hiding twice is hiding once");
    check(store_rules::withHidden(store_rules::withHidden(kSomeFlags, true), false) == kSomeFlags, "hide then show gives the flags back");
}

}  // namespace

int main() {
    testLevelMenuRoute();
    testAnswerRoute();
    testWithHidden();
    return g_failures == 0 ? 0 : 1;
}
