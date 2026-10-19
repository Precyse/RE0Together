// The pure rules of milestone 17 wave 3, walks and the Mayhem counter (no game): the end of a ragdoll, the counter and score block
// a Mayhem destruction goes to, and the bonus a counter pays.
#include <cstdio>
#include <limits>

#include "../src/walk_rules.h"

namespace {

using walk_rules::Side;

constexpr float kEpsilon = 0.01f;
constexpr int32_t kPerUnit = 5;
constexpr uint32_t kFirstIndex = 1;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

void theRagdollEnd() {
    check(!walk_rules::ragdollEnds(0, 0) && !walk_rules::ragdollEnds(0, 1) && !walk_rules::ragdollEnds(0, 0xFF), "no ragdoll byte, no end");
    check(!walk_rules::ragdollEnds(1, 0), "a ragdoll whose flag bit is clear does not end");
    check(walk_rules::ragdollEnds(1, 1), "a ragdoll with the flag bit ends");
    check(walk_rules::ragdollEnds(1, 0xFF), "only bit 0 of the flags counts, the others may be set");
    check(!walk_rules::ragdollEnds(1, 0xFE), "bit 0 clear with the others set does not end");
}

void theDestructionCounter() {
    check(walk_rules::destructionCounter(true, false, false) == Side::Local, "the local player's destruction is the local counter");
    check(walk_rules::destructionCounter(true, true, true) == Side::Local, "local wins over every other flag");
    check(walk_rules::destructionCounter(false, true, true) == Side::Game, "the default partner stays in the game's counter");
    check(walk_rules::destructionCounter(false, true, false) == Side::Remote, "another remote has a counter of its own");
    check(walk_rules::destructionCounter(false, false, false) == Side::Game, "an NPC or no destroyer stays in the game's counter");
    check(walk_rules::destructionCounter(false, false, true) == Side::Game, "the default partner flag alone is the game's counter");
}

void theScoreBlock() {
    check(walk_rules::scoreBlock(0, false, false) == Side::Local && walk_rules::scoreBlock(0, true, false) == Side::Local &&
              walk_rules::scoreBlock(0, true, true) == Side::Local,
          "index 0 is the local block whatever the destroyer");
    check(walk_rules::scoreBlock(kFirstIndex, true, true) == Side::Game, "index 1 with the default partner is the game's second block");
    check(walk_rules::scoreBlock(kFirstIndex, true, false) == Side::Remote, "index 1 with another remote is the remote's own block");
    check(walk_rules::scoreBlock(kFirstIndex, false, false) == Side::Game, "index 1 with a destroyer that is no remote is the game's second block");
}

void thePaidBonus() {
    check(!walk_rules::bonusFor(0, 1.0f, kEpsilon, kPerUnit).pay && !walk_rules::bonusFor(-3, 1.0f, kEpsilon, kPerUnit).pay,
          "a count of zero or less pays nothing");
    const walk_rules::Bonus unit = walk_rules::bonusFor(3, 1.0f, kEpsilon, kPerUnit);
    check(unit.pay && unit.kind == walk_rules::kBonusPerUnit && unit.amount == 15, "multiplier 1.0, 5 a unit, count 3 pays 15 by the unit");
    check(!walk_rules::bonusFor(3, 1.0f, kEpsilon, 0).pay, "multiplier 1.0 and no per-unit bonus pays nothing");
    const walk_rules::Bonus scaled = walk_rules::bonusFor(3, 1.5f, kEpsilon, kPerUnit);
    check(scaled.pay && scaled.kind == walk_rules::kBonusScaled && scaled.amount == 4, "multiplier 1.5, count 3 pays 4 (4.5 truncated)");
    check(walk_rules::bonusFor(3, 1.5f, kEpsilon, 0).pay, "a scaled bonus does not need the per-unit bonus");
    const walk_rules::Bonus nearOne = walk_rules::bonusFor(3, 0.995f, kEpsilon, kPerUnit);
    check(nearOne.pay && nearOne.kind == walk_rules::kBonusPerUnit && nearOne.amount == 15, "multiplier 0.995 is within the epsilon of 1.0: the unit path");
    const walk_rules::Bonus onTheEdge = walk_rules::bonusFor(2, 1.5f, 0.5f, kPerUnit);
    check(onTheEdge.pay && onTheEdge.kind == walk_rules::kBonusScaled && onTheEdge.amount == 3, "a distance of exactly the epsilon is scaled");
    const walk_rules::Bonus below = walk_rules::bonusFor(2, 0.5f, kEpsilon, kPerUnit);
    check(below.pay && below.kind == walk_rules::kBonusScaled && below.amount == 1, "a multiplier below 1.0 further than the epsilon is scaled");
    const walk_rules::Bonus nan = walk_rules::bonusFor(3, std::numeric_limits<float>::quiet_NaN(), kEpsilon, kPerUnit);
    check(nan.pay && nan.kind == walk_rules::kBonusScaled && nan.amount == INT32_MIN, "a multiplier that is not a number is scaled, with the game's integer indefinite");
}

void theTruncation() {
    check(walk_rules::truncateLikeGame(4.5) == 4 && walk_rules::truncateLikeGame(-4.5) == -4, "a value is truncated toward zero");
    check(walk_rules::truncateLikeGame(3e10) == INT32_MIN && walk_rules::truncateLikeGame(-3e10) == INT32_MIN, "a value that does not fit is the integer indefinite");
    check(walk_rules::truncateLikeGame(2147483647.9) == INT32_MAX, "the highest value that fits is kept");
}

}  // namespace

int main() {
    theRagdollEnd();
    theDestructionCounter();
    theScoreBlock();
    thePaidBonus();
    theTruncation();
    return g_failures == 0 ? 0 : 1;
}
