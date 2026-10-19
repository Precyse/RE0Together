// The population rules of milestone 9 (no game): the area budget for any number of players, the cluster rule, and that
// each reduces to the shipped game's answer with one remote.
#include <cmath>
#include <cstdio>

#include "../src/population_rules.h"

namespace {

using population_rules::Peer;
using population_rules::Point;

constexpr float kRadius = 70.0f;
constexpr uint32_t kTrafficBase = 0x1A;
constexpr uint32_t kHumansBase = 0x96;
constexpr uint32_t kParkedBase = 0x0C;
constexpr float kClusterRadius = 160.0f;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

Point at(float x) { return {x, 0, 0}; }

// The shipped function 0x93E8D0 for one partner: half the base within the radius, the whole base from twice the radius,
// the half plus the fraction of it in between.
uint32_t shipped(uint32_t base, float apart, float radius) {
    const uint32_t half = base >> 1;
    if (apart * apart < radius * radius) return half;
    if (apart * apart > 4 * radius * radius) return base;
    const double fraction = (static_cast<double>(apart) - radius) / radius;
    return half + static_cast<uint32_t>(half * static_cast<double>(static_cast<float>(fraction)));
}

void oneRemoteIsTheShippedBudget() {
    bool same = true;
    for (const uint32_t base : {0x0Cu, kTrafficBase, 0x22u, 0x30u}) {
        for (float apart = 10; apart < 200; apart += 3.5f) {
            const Point remote = at(apart);
            const uint32_t mine = population_rules::budget(base, population_rules::extraAreas(at(0), &remote, 1, kRadius));
            const uint32_t theirs = shipped(base, apart, kRadius);
            same = same && (mine == theirs || mine + 1 == theirs || theirs + 1 == mine);
        }
    }
    check(same, "one remote: within one of the shipped budget at every distance");
    const Point near = at(60), far = at(150);
    check(population_rules::budget(kTrafficBase, population_rules::extraAreas(at(0), &near, 1, kRadius)) == kTrafficBase / 2,
          "one remote within the radius: half the base");
    check(population_rules::budget(kTrafficBase, population_rules::extraAreas(at(0), &far, 1, kRadius)) == kTrafficBase,
          "one remote beyond twice the radius: the whole base");
}

void areasCountSeparatePlayerAreas() {
    const Point together[] = {at(10), at(20), at(30)};
    check(population_rules::budget(kTrafficBase, population_rules::extraAreas(at(0), together, 3, kRadius)) == kTrafficBase / 2,
          "four players together: one area, half the base");
    const Point pairAway[] = {at(500), at(510)};
    check(population_rules::budget(kTrafficBase, population_rules::extraAreas(at(0), pairAway, 2, kRadius)) == kTrafficBase,
          "two remotes together far from the local player: two areas, the whole base");
    const Point pairHere[] = {at(10), at(20)};
    check(population_rules::budget(kTrafficBase, population_rules::extraAreas(at(0), pairHere, 2, kRadius)) == kTrafficBase / 2,
          "three players together: one area");
    const Point one[] = {at(105)};
    const uint32_t partial = population_rules::budget(kTrafficBase, population_rules::extraAreas(at(0), one, 1, kRadius));
    check(partial > kTrafficBase / 2 && partial < kTrafficBase, "a remote halfway between near and far counts part of an area");
}

void theBudgetNeverPassesTheShippedMaximum() {
    const Point twoApart[] = {at(500), at(1000)};
    const Point threeApart[] = {at(500), at(1000), at(1500)};
    for (const uint32_t base : {kTrafficBase, kHumansBase, kParkedBase}) {
        check(population_rules::budget(base, population_rules::extraAreas(at(0), twoApart, 2, kRadius)) == base,
              "three players apart: the base, which the game gives two players apart");
        check(population_rules::budget(base, population_rules::extraAreas(at(0), threeApart, 3, kRadius)) == base,
              "four players apart: still the base, a quarter of it per area");
    }
    check(population_rules::budget(kTrafficBase, 1.0f) == kTrafficBase && population_rules::budget(kTrafficBase, 3.0f) == kTrafficBase,
          "extra areas above one add nothing");
    check(population_rules::budget(kTrafficBase, 0.0f) == kTrafficBase / 2, "no extra area: half the base");
    check(population_rules::budget(kTrafficBase, 0.5f) == kTrafficBase / 2 + kTrafficBase / 4, "half an extra area: half of the other half");
    const Point halfway[] = {at(105), at(5000)};
    const float extra = population_rules::extraAreas(at(0), halfway, 2, kRadius);
    check(extra > 1.0f && population_rules::areasHundredths(extra) == 250,
          "the census still reports the uncapped number of areas (2.50 here)");
}

void vehicleHeadroomTakesFourPerRemote() {
    check(population_rules::vehicleHeadroom(20, 0) == 20 && population_rules::vehicleHeadroom(20, 1) == 16,
          "no remote: nothing off; one remote: 4 off, as the shipped function does");
    check(population_rules::vehicleHeadroom(20, 3) == 8 && population_rules::vehicleHeadroom(2, 3) == -10,
          "three remote members: 12 off (it can go negative, the caller compares)");
    check(population_rules::vehicleHeadroom(28, 0) == 28 && population_rules::vehicleHeadroom(28, 1) == 24 && population_rules::vehicleHeadroom(28, 2) == 20,
          "a joiner with no remote human yet but members in the session: 4 off per member");
}

void theLowestSlotOfAClusterAnswersFar() {
    const Peer farRemote[] = {{1, at(1000)}};
    const Peer nearHost[] = {{0, at(50)}};
    const Peer nearGuest[] = {{2, at(50)}};
    check(population_rules::clusterFar(true, 0, at(0), farRemote, 1, kClusterRadius), "host, one remote far: far, as shipped");
    check(!population_rules::clusterFar(true, 0, at(0), nearGuest, 1, kClusterRadius), "host, one remote near: near, as shipped");
    check(population_rules::clusterFar(false, 1, at(0), farRemote, 1, kClusterRadius), "guest, the host far: far, as shipped");
    check(!population_rules::clusterFar(false, 1, at(0), nearHost, 1, kClusterRadius), "guest, the host near: near, as shipped");

    const Peer hostFarGuestNear[] = {{0, at(1000)}, {1, at(50)}};
    check(!population_rules::clusterFar(false, 2, at(0), hostFarGuestNear, 2, kClusterRadius),
          "guest 3 with guest 2 near and the host far: near, guest 2 leads the cluster");
    const Peer hostFarGuest3Near[] = {{0, at(1000)}, {2, at(50)}};
    check(population_rules::clusterFar(false, 1, at(0), hostFarGuest3Near, 2, kClusterRadius),
          "guest 2 with guest 3 near and the host far: far, it leads the cluster");
    const Peer hostNearGuests[] = {{1, at(30)}, {2, at(60)}};
    check(!population_rules::clusterFar(true, 0, at(0), hostNearGuests, 2, kClusterRadius), "host with both guests near: near");
    check(!population_rules::clusterFar(false, 2, at(0), hostNearGuests, 2, kClusterRadius), "guest 3 with a lower slot near: near");
    check(population_rules::clusterFar(true, 0, at(0), nullptr, 0, kClusterRadius), "no remote: far");

}

void areaCountForTheCensus() {
    check(population_rules::areasHundredths(0.0f) == 100, "the local area alone is 1.00");
    check(population_rules::areasHundredths(1.5f) == 250, "the local area and one and a half more is 2.50");
    check(population_rules::areasHundredths(0.994f) == 199, "hundredths are rounded");
}

}  // namespace

int main() {
    oneRemoteIsTheShippedBudget();
    areasCountSeparatePlayerAreas();
    theBudgetNeverPassesTheShippedMaximum();
    theLowestSlotOfAClusterAnswersFar();
    vehicleHeadroomTakesFourPerRemote();
    areaCountForTheCensus();
    return g_failures == 0 ? 0 : 1;
}
