// The placement spread rules of milestone 9 (no game): guest ranks and steps along the right-hand direction.
#include <cmath>
#include <cstdio>

#include "../src/spread_rules.h"
#include "check.h"

namespace {

using namespace spread_rules;

bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

}  // namespace

int main() {
    const uint8_t slots[] = {0, 1, 2, 3};
    check(guestRank(0, 0, slots, 4) == 0, "the host has rank 0");
    check(guestRank(1, 0, slots, 4) == 0 && guestRank(2, 0, slots, 4) == 1 && guestRank(3, 0, slots, 4) == 2,
          "guests are ranked by slot, the lowest guest first");
    const uint8_t pair[] = {0, 1};
    check(guestRank(1, 0, pair, 2) == 0, "with two players the guest has rank 0: the shipped placement (the 2-player case)");
    const uint8_t gap[] = {0, 2, 3};
    check(guestRank(3, 0, gap, 3) == 1, "a free slot does not count");

    const Offset none = stepsAlong(0, 1, 0);
    check(near(none.x, 0) && near(none.z, 0), "rank 0 stays where it is");
    const Offset one = stepsAlong(1, 3, 4);
    check(near(one.x, 0.6f * kPlayerSpread) && near(one.z, 0.8f * kPlayerSpread), "a step is kPlayerSpread along the unit direction");
    const Offset two = stepsAlong(2, 0, -5);
    check(near(two.x, 0) && near(two.z, -2 * kPlayerSpread), "rank 2 is two steps");
    const Offset flat = stepsAlong(1, 0, 0);
    check(near(flat.x, kPlayerSpread) && near(flat.z, 0), "a direction without length falls back to the x axis");

    const Offset right = rightOf(0, 1);
    check(near(right.x, 1) && near(right.z, 0), "looking along +z, the right-hand side is +x");
    const Offset rightAgain = rightOf(1, 0);
    check(near(rightAgain.x, 0) && near(rightAgain.z, -1), "looking along +x, the right-hand side is -z");
    return g_failures == 0 ? 0 : 1;
}
