// The pure rules of milestone 17 wave 3, FUZZ (no game): the roll that picks a remote, the riot "near" test and the crime area test.
#include <cmath>
#include <cstdio>
#include <limits>
#include <set>

#include "../src/fuzz_rules.h"
#include "check.h"

namespace {

using fuzz_rules::kNoIndex;

constexpr uint32_t kFirstRoll = 270369;          // xorshift32 from the state 1
constexpr uint32_t kSecondRoll = 67634689;
constexpr uint32_t kThirdRoll = 2647435461u;
constexpr int kRollSteps = 10000;
constexpr uint32_t kSweepRolls = 300;
constexpr double kRadiusSquared = 100.0;
constexpr float kWindow = 4.0f;
constexpr uint32_t kCrimeKey = 0x3A;

void theRoll() {
    const uint32_t first = fuzz_rules::nextRoll(1);
    const uint32_t second = fuzz_rules::nextRoll(first);
    check(first == kFirstRoll && second == kSecondRoll && fuzz_rules::nextRoll(second) == kThirdRoll, "the roll from the state 1 is the xorshift32 sequence 270369, 67634689, 2647435461");
    check(fuzz_rules::nextRoll(0) == fuzz_rules::nextRoll(fuzz_rules::kFallbackSeed) && fuzz_rules::nextRoll(0) != 0, "a zero state is replaced by the fallback seed");
    for (const uint32_t seed : {1u, 0u, 0xFFFFFFFFu}) {
        uint32_t state = seed;
        bool neverZero = true;
        for (int step = 0; step < kRollSteps; ++step) {
            state = fuzz_rules::nextRoll(state);
            neverZero = neverZero && state != 0;
        }
        check(neverZero, "the roll is never 0 over 10,000 steps");
    }
}

void thePick() {
    check(fuzz_rules::pickIndex(12345, 0) == kNoIndex, "no remotes: no index");
    bool onlyOne = true;
    for (uint32_t roll = 0; roll < kSweepRolls; ++roll) onlyOne = onlyOne && fuzz_rules::pickIndex(roll, 1) == 0;
    check(onlyOne, "one remote is always index 0");
    for (const size_t count : {size_t{2}, size_t{3}}) {
        std::set<size_t> reached;
        bool inRange = true;
        for (uint32_t roll = 0; roll < kSweepRolls; ++roll) {
            const size_t index = fuzz_rules::pickIndex(roll, count);
            inRange = inRange && index < count;
            reached.insert(index);
        }
        check(inRange && reached.size() == count, count == 2 ? "two remotes: both indexes are reached, none beyond" : "three remotes: every index is reached, none beyond");
    }
    uint32_t state = 1;
    std::set<size_t> reached;
    for (int step = 0; step < kRollSteps; ++step) {
        state = fuzz_rules::nextRoll(state);
        reached.insert(fuzz_rules::pickIndex(state, 3));
    }
    check(reached.size() == 3, "the generator reaches every one of three remotes");
}

void theRioterTest() {
    const float inside = 99.0f;
    const float gapInside = 3.0f;
    check(fuzz_rules::rioterNear(inside, kRadiusSquared, gapInside, kWindow), "inside the radius and the window: near");
    check(fuzz_rules::rioterNear(100.0f, kRadiusSquared, gapInside, kWindow), "a distance equal to the radius is near");
    check(!fuzz_rules::rioterNear(inside, kRadiusSquared, kWindow, kWindow), "a gap equal to the window is not near");
    check(fuzz_rules::rioterNear(inside, kRadiusSquared, std::nextafter(kWindow, 0.0f), kWindow), "a gap just inside the window is near");
    check(!fuzz_rules::rioterNear(std::nextafter(100.0f, 200.0f), kRadiusSquared, gapInside, kWindow), "a distance just outside the radius is not near");
    check(!fuzz_rules::rioterNear(inside, kRadiusSquared, std::nextafter(kWindow, 10.0f), kWindow), "a gap just outside the window is not near");
    constexpr float kNan = std::numeric_limits<float>::quiet_NaN();
    constexpr float kInfinity = std::numeric_limits<float>::infinity();
    check(!fuzz_rules::rioterNear(kNan, kRadiusSquared, gapInside, kWindow), "a NaN distance is not near");
    check(!fuzz_rules::rioterNear(inside, kRadiusSquared, kNan, kWindow), "a NaN gap is not near");
    check(!fuzz_rules::rioterNear(inside, std::numeric_limits<double>::quiet_NaN(), gapInside, kWindow), "a NaN radius is not near");
    check(!fuzz_rules::rioterNear(kInfinity, kRadiusSquared, gapInside, kWindow), "an infinite distance is not near");
    check(!fuzz_rules::rioterNear(inside, kRadiusSquared, kInfinity, kWindow), "an infinite gap is not near");
    check(fuzz_rules::rioterNear(kInfinity, std::numeric_limits<double>::infinity(), gapInside, kWindow), "an infinite distance within an infinite radius is near");
}

void theCrimeDistrict() {
    constexpr uintptr_t kRecord = 0x2530000;
    check(!fuzz_rules::inCrimeDistrict(0, kCrimeKey, kCrimeKey), "no district record: not in the crime area");
    check(fuzz_rules::inCrimeDistrict(kRecord, kCrimeKey, kCrimeKey), "a record with the crime's key: in the crime area");
    check(!fuzz_rules::inCrimeDistrict(kRecord, kCrimeKey + 1, kCrimeKey), "a record with another key: not in the crime area");
    check(fuzz_rules::inCrimeDistrict(kRecord, 0, 0), "a zero key matches a zero key, as the game's compare does");
    check(!fuzz_rules::inCrimeDistrict(0, 0, 0), "a zero key without a record is not in the crime area");
}

}  // namespace

int main() {
    theRoll();
    thePick();
    theRioterTest();
    theCrimeDistrict();
    return g_failures == 0 ? 0 : 1;
}
