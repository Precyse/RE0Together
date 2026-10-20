// The pure rules of the outfit, item and weapon slot sync masks (no game).
#include <cstdio>

#include "../src/outfit_sync_rules.h"
#include "check.h"

namespace {

using outfit_sync_rules::Route;

constexpr uint32_t kLocalMask = 1;
constexpr uint32_t kRemoteMask = 2;
constexpr uint32_t kAllMask = 3;

uint32_t targeted(uint32_t base, uint32_t number) { return base + outfit_sync_rules::kTargetStep * number; }

void testTargetNumber() {
    using outfit_sync_rules::targetNumber;
    check(targetNumber(kLocalMask) == 0 && targetNumber(kRemoteMask) == 0 && targetNumber(kAllMask) == 0, "the shipped masks name nobody");
    check(targetNumber(50) == 3 && targetNumber(51) == 3, "2 + 16 * 3 and 3 + 16 * 3 name player 3");
    check(targetNumber(66) == 4, "2 + 16 * 4 names player 4");
    check(targetNumber(targeted(kRemoteMask, 2)) == 2 && targetNumber(targeted(kAllMask, 12)) == 12, "players 2 and 12 are the ends");
    check(targetNumber(49) == 0, "a number without the remote bit names nobody");
    check(targetNumber(18) == 0, "the number 1 is the host, not a remote");
    check(targetNumber(targeted(kRemoteMask, 13)) == 0, "the number 13 is out of range");
    check(targetNumber(0xFFFFFFFF) == 0, "a mask of all ones names nobody");
    check(targetNumber(0) == 0, "mask 0 names nobody");
}

void testNamesPlayer() {
    using outfit_sync_rules::namesPlayer;
    check(namesPlayer("#PLAYER3#", 3), "#PLAYER3# is player 3");
    check(namesPlayer("#PLAYER12#", 12), "#PLAYER12# is player 12");
    check(!namesPlayer("#PLAYER31#", 3), "#PLAYER31# is not player 3");
    check(!namesPlayer("#PLAYER3", 3), "a name without the closing mark is not player 3");
    check(!namesPlayer("#player3#", 3), "the name is case sensitive");
    check(!namesPlayer("", 3), "an empty name is nobody");
    check(!namesPlayer("#PLAYER3#", 4), "player 3 is not player 4");
}

void testRoute() {
    using outfit_sync_rules::route;
    check(route(kRemoteMask, false) == Route::Original && route(kRemoteMask, true) == Route::Original, "no number: the game's own call");
    check(route(50, true) == Route::ToTarget, "a number whose player is found goes to that player");
    check(route(50, false) == Route::Nothing, "a number whose player is gone sends nothing");
}

void testLocalPart() {
    using outfit_sync_rules::localPart;
    check(localPart(kLocalMask) == 1 && localPart(kRemoteMask) == 0 && localPart(kAllMask) == 1, "the local part is bit 0");
    check(localPart(51) == 1 && localPart(50) == 0, "a targeted mask keeps only its local bit");
}

}  // namespace

int main() {
    testTargetNumber();
    testNamesPlayer();
    testRoute();
    testLocalPart();
    std::printf("%s\n", g_failures ? "FAILED" : "all passed");
    return g_failures ? 1 : 0;
}
