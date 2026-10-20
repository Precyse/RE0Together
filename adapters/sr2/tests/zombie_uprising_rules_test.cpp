// The pure rules of Zombie Uprising for 3 or 4 players (no game): start spots, the health average, the spawn picker's players.
#include <cstdio>
#include <cstring>
#include <iterator>
#include <span>

#include "../src/spread_rules.h"
#include "../src/zombie_uprising_rules.h"
#include "check.h"

namespace {

namespace rules = zombie_uprising_rules;
using partner_rules::Vec3;

constexpr Vec3 kMarker = {10.0f, 2.0f, -4.0f};
constexpr float kPartnerOffset = 1.0f;
constexpr float kEpsilon = 1e-5f;
constexpr uint32_t kLocal = 0x02C00000;
constexpr uint32_t kFirstRemote = 0x02C01000;
constexpr uint32_t kSecondRemote = 0x02C02000;
constexpr uint32_t kThirdRemote = 0x02C03000;
constexpr uint32_t kNobody = 0;
constexpr float kPairs[][2] = {{0.25f, 0.75f}, {1.0f, 0.0f}, {0.3f, 0.7f}, {0.123f, 0.456f}, {0.0f, 0.0f}};

bool almostEqual(float a, float b) { return a - b < kEpsilon && b - a < kEpsilon; }

void testStartSpot() {
    const Vec3 first = rules::startSpot(kMarker, kPartnerOffset, 0);
    check(almostEqual(first.x, kMarker.x + kPartnerOffset) && first.y == kMarker.y && first.z == kMarker.z, "rank 0 is the shipped partner spot (x + 1, y, z)");
    check(almostEqual(rules::startSpot(kMarker, kPartnerOffset, 1).x, kMarker.x + kPartnerOffset + spread_rules::kPlayerSpread), "rank 1 is one spread step further");
    check(almostEqual(rules::startSpot(kMarker, kPartnerOffset, 2).x, kMarker.x + kPartnerOffset + 2 * spread_rules::kPlayerSpread), "rank 2 is two steps further");
    const Vec3 third = rules::startSpot(kMarker, kPartnerOffset, 2);
    check(third.y == kMarker.y && third.z == kMarker.z, "y and z are kept for every rank");
    check(almostEqual(rules::startSpot(kMarker, kPartnerOffset, 1).x, 12.5f) && almostEqual(third.x, 14.0f), "P3 stands at +2.5 and P4 at +4.0 from the local player");
}

void testHealthFraction() {
    check(rules::healthFraction(50.0f, 100) == 0.5f, "half health is 0.5");
    check(rules::healthFraction(100.0f, 100) == 1.0f, "full health is 1");
    check(rules::healthFraction(50.0f, 0) == 0.0f, "no maximum gives 0");
    check(rules::healthFraction(50.0f, -10) == 0.0f, "a negative maximum gives 0");
}

void testAverageHealth() {
    bool sameBits = true;
    for (const auto& pair : kPairs) {
        const float shipped = (pair[0] + pair[1]) * 0.5f;
        const float remotes[] = {pair[1]};
        const float average = rules::averageHealth(pair[0], remotes);
        sameBits = sameBits && std::memcmp(&shipped, &average, sizeof(float)) == 0;
    }
    check(sameBits, "with one remote the average has the bits of the shipped (l + p) * 0.5");
    const float three[] = {0.3f, 0.6f, 0.9f};
    check(almostEqual(rules::averageHealth(0.0f, three), 0.45f), "three remotes give the mean of the four");
    check(rules::averageHealth(0.4f, std::span<const float>()) == 0.4f, "no remote gives the local fraction");
}

void testFractionsText() {
    const float two[] = {0.5f, 1.0f};
    check(rules::fractionsText(two) == "0.500 1.000", "the fractions are listed with three decimals");
    check(rules::fractionsText(std::span<const float>()).empty(), "no fractions give no text");
}

void testSpawnPlayers() {
    const uint32_t pair[] = {kLocal, kFirstRemote};
    const uintptr_t none[1] = {0};
    check(rules::spawnExtrasOf(none, 0).extraCount == 0, "an empty walk has no extras");
    const uintptr_t one[] = {kFirstRemote};
    check(rules::spawnExtrasOf(one, 1).extraCount == 0, "one remote is the game's pair alone");
    const uintptr_t three[] = {kFirstRemote, kSecondRemote, kThirdRemote};
    const rules::SpawnPlayers extras = rules::spawnExtrasOf(three, 3);
    check(extras.extraCount == 2 && extras.extras[0] == kSecondRemote && extras.extras[1] == kThirdRemote, "three remotes: the second and third are the extras");
    check(rules::spawnPlayerAt(pair, extras, 0) == kLocal && rules::spawnPlayerAt(pair, extras, 1) == kFirstRemote, "index 0 and 1 are the game's pair");
    check(rules::spawnPlayerAt(pair, extras, 2) == kSecondRemote && rules::spawnPlayerAt(pair, extras, 3) == kThirdRemote, "index 2 and 3 are the extras");
    check(rules::spawnPlayerAt(pair, extras, 4) == kNobody, "an index past the extras is nobody");

    uintptr_t crowd[rules::kSpawnPlayerSlots + 4];
    for (size_t i = 0; i < std::size(crowd); ++i) crowd[i] = 0x02D00000 + i * 0x1000;
    const rules::SpawnPlayers capped = rules::spawnExtrasOf(crowd, std::size(crowd));
    check(capped.extraCount == rules::kSpawnPlayerSlots - rules::kSpawnPairSlots, "a walk longer than the capacity is cut to the extras that fit");
    check(rules::spawnPlayerAt(pair, capped, rules::kSpawnPlayerSlots - 1) == crowd[rules::kSpawnPlayerSlots - rules::kSpawnPairSlots], "the last slot holds the last extra that fits");
    check(rules::spawnPlayerAt(pair, capped, rules::kSpawnPlayerSlots) == kNobody && rules::spawnPlayerAt(pair, capped, rules::kSpawnPlayerSlots + 5) == kNobody,
          "an index at or past the slot count is nobody");
}

}  // namespace

int main() {
    testStartSpot();
    testHealthFraction();
    testAverageHealth();
    testFractionsText();
    testSpawnPlayers();
    return g_failures == 0 ? 0 : 1;
}
