// The pure rules of Trail Blazing for 3 or 4 players (no game): the water test and the checkpoint distance.
#include <cmath>
#include <limits>

#include "../src/trail_blazing_rules.h"
#include "check.h"

namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kDeep = 5.0f;
constexpr float kJustOver = 1.0001f;
constexpr float kDistance = 100.0f;

void theWater() {
    using trail_blazing_rules::inWater;
    check(inWater(1, 0, 0.0f), "swimming alone is in water");
    check(inWater(-1, 0, 0.0f), "any non-zero swimming value is in water");
    check(!inWater(0, 0, 0.0f), "on foot and dry is not in water");
    check(!inWater(0, trail_blazing_rules::kSubmergedBit, 1.0f), "submerged to exactly 1.0 is not in water");
    check(inWater(0, trail_blazing_rules::kSubmergedBit, kJustOver), "submerged to 1.0001 is in water");
    check(!inWater(0, trail_blazing_rules::kSubmergedBit, kNaN), "a NaN depth is not in water");
    check(!inWater(0, 0, kDeep), "a depth of 5 without the submerged flag is not in water");
    check(inWater(0, 0x81, kDeep), "the submerged bit counts among other flag bits");
    check(!inWater(0, 0x80, kDeep), "other flag bits alone do not count");
}

void theCheckpointDistance() {
    using trail_blazing_rules::closer;
    check(closer(kDistance, 50.0f) == 50.0f, "a smaller candidate replaces the distance");
    check(closer(kDistance, kDistance) == kDistance, "an equal candidate replaces it (the game's tie rule)");
    check(closer(kDistance, 150.0f) == kDistance, "a larger candidate is ignored");
    check(closer(kDistance, kNaN) == kDistance, "a NaN candidate keeps the distance");
    check(std::isnan(closer(kNaN, 50.0f)), "a NaN distance stays NaN");
    check(trail_blazing_rules::countsAtCheckpoint(true, true) && trail_blazing_rules::countsAtCheckpoint(true, false),
          "the default partner always counts");
    check(trail_blazing_rules::countsAtCheckpoint(false, true), "another remote counts while it rides");
    check(!trail_blazing_rules::countsAtCheckpoint(false, false), "another remote on foot does not count");
}

}  // namespace

int main() {
    theWater();
    theCheckpointDistance();
    return g_failures == 0 ? 0 : 1;
}
