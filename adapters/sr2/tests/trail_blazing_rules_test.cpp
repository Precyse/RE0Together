// The pure rules of Trail Blazing for 3 or 4 players (no game): the water test, the checkpoint distance, the start grid and the rank of
// each remote on it.
#include <cmath>
#include <cstdio>
#include <limits>

#include "../src/trail_blazing_rules.h"

namespace {

using partner_rules::Vec3;
using trail_blazing_rules::Cell;

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr size_t kNone = partner_rules::kNone;
constexpr float kSpacing = 5.0f;
constexpr float kDeep = 5.0f;
constexpr float kJustOver = 1.0001f;
constexpr float kDistance = 100.0f;
constexpr float kTolerance = 1e-5f;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

bool sameCell(Cell cell, uint32_t right, uint32_t back) { return cell.right == right && cell.back == back; }

bool sameVec(const Vec3& a, const Vec3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

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

void theGrid() {
    check(sameCell(trail_blazing_rules::startCell(0), 1, 0), "rank 0 is the shipped partner spot, one to the right");
    check(sameCell(trail_blazing_rules::startCell(1), 0, 1), "rank 1 is one row behind the local player");
    check(sameCell(trail_blazing_rules::startCell(2), 1, 1), "rank 2 is one row behind the partner");
    check(sameCell(trail_blazing_rules::startCell(3), 0, 2), "rank 3 is two rows behind the local player");
}

void thePositions() {
    const Vec3 first = {10.0f, 2.0f, -4.0f};
    const Vec3 axisRight = {1.0f, 0.0f, 0.0f};
    const Vec3 axisForward = {0.0f, 0.0f, 1.0f};
    check(sameVec(trail_blazing_rules::startPosition(first, axisRight, axisForward, kSpacing, {1, 0}), {15.0f, 2.0f, -4.0f}),
          "an axis-aligned record: one to the right adds 5 along the right row");
    check(sameVec(trail_blazing_rules::startPosition(first, axisRight, axisForward, kSpacing, {0, 1}), {10.0f, 2.0f, -9.0f}),
          "an axis-aligned record: one row behind subtracts 5 along the forward row");
    check(sameVec(trail_blazing_rules::startPosition(first, axisRight, axisForward, kSpacing, {1, 1}), {15.0f, 2.0f, -9.0f}),
          "an axis-aligned record: the rank 2 cell");
    check(sameVec(trail_blazing_rules::startPosition(first, axisRight, axisForward, kSpacing, {0, 0}), first), "cell {0, 0} is the first spot");

    const Vec3 rotatedRight = {0.6f, 0.0f, 0.8f};
    const Vec3 rotatedForward = {-0.8f, 0.0f, 0.6f};
    const Vec3 rotated = trail_blazing_rules::startPosition({0.0f, 0.0f, 0.0f}, rotatedRight, rotatedForward, kSpacing, {1, 1});
    check(std::fabs(rotated.x - (kSpacing * 0.6f + kSpacing * 0.8f)) < kTolerance && rotated.y == 0.0f &&
              std::fabs(rotated.z - (kSpacing * 0.8f - kSpacing * 0.6f)) < kTolerance,
          "a rotated record gives the hand values 7 and 1");
}

void theRanks() {
    using trail_blazing_rules::placementRank;
    check(placementRank(0, 0) == 0 && placementRank(1, 0) == 1 && placementRank(2, 0) == 2, "default at 0: ranks 0, 1, 2");
    check(placementRank(0, 1) == 1 && placementRank(1, 1) == 0 && placementRank(2, 1) == 2, "default at 1: ranks 1, 0, 2");
    check(placementRank(0, 2) == 1 && placementRank(1, 2) == 2 && placementRank(2, 2) == 0, "default at 2: ranks 1, 2, 0");
    check(placementRank(0, kNone) == 1 && placementRank(1, kNone) == 2 && placementRank(2, kNone) == 3, "no default: every remote ranks after cell 0");
}

}  // namespace

int main() {
    theWater();
    theCheckpointDistance();
    theGrid();
    thePositions();
    theRanks();
    return g_failures == 0 ? 0 : 1;
}
