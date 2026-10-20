// The population rules of milestone 9 (no game): the area budget for any number of players, the cluster rule, and that
// each reduces to the shipped game's answer with one remote.
#include <cmath>
#include <cstdio>
#include <vector>

#include "../src/population_rules.h"
#include "check.h"

namespace {

using population_rules::Peer;
using population_rules::Point;

constexpr float kRadius = 70.0f;
constexpr uint32_t kTrafficBase = 0x1A;
constexpr uint32_t kHumansBase = 0x96;
constexpr uint32_t kParkedBase = 0x0C;
constexpr float kClusterRadius = 160.0f;

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

constexpr int32_t kShippedPool = 32;
constexpr int32_t kGrownPool = 64;
constexpr int32_t kClass1Limit = 12;
constexpr size_t kOneRemote = 1;

// The shipped function 0x93C1D0: 0x20 less the vehicles in use, 4 less while a partner exists.
int32_t shippedHeadroom(int32_t inUse, size_t remoteMembers) { return kShippedPool - inUse - (remoteMembers > 0 ? 4 : 0); }

void vehicleHeadroomTakesFourPerRemote() {
    check(population_rules::poolHeadroom(kShippedPool, 12, 0) == 20 && population_rules::poolHeadroom(kShippedPool, 12, 1) == 16,
          "no remote: nothing off; one remote: 4 off, as the shipped function does");
    check(population_rules::poolHeadroom(kShippedPool, 12, 3) == 8 && population_rules::poolHeadroom(kShippedPool, 30, 3) == -10,
          "three remote members: 12 off (it can go negative, the caller compares)");
    check(population_rules::poolHeadroom(kShippedPool, 4, 0) == 28 && population_rules::poolHeadroom(kShippedPool, 4, 1) == 24 &&
              population_rules::poolHeadroom(kShippedPool, 4, 2) == 20,
          "a joiner with no remote human yet but members in the session: 4 off per member");
    check(population_rules::poolHeadroom(kGrownPool, 30, 3) == 22 && population_rules::poolHeadroom(kGrownPool, 30, 1) == 30,
          "the 64 vehicle pool: 64 less 4 per member less the vehicles in use");
    check(population_rules::poolHeadroom(kGrownPool, 30, 0) == 2, "the 64 vehicle pool with no remote member keeps the shipped 32");
}

void oneRemoteIsTheShippedHeadroomAtEveryCount() {
    bool same = true;
    for (int32_t inUse = 0; inUse <= kShippedPool; ++inUse) {
        for (const size_t members : {size_t{0}, kOneRemote}) {
            // The quota arguments are arbitrary: with one remote member or none they are not read.
            same = same && population_rules::vehicleHeadroom(kShippedPool, inUse, members, 0, 100) == shippedHeadroom(inUse, members);
        }
    }
    check(same, "with no remote member or one the headroom is the shipped function's answer at every vehicle count");
}

population_rules::Quota quotaOf(const std::vector<population_rules::Player>& players, uint8_t localSlot, int32_t limit = kClass1Limit) {
    std::vector<uint8_t> leaders(players.size());
    population_rules::trafficLeaders(players.data(), players.size(), kClusterRadius, leaders.data());
    return population_rules::trafficQuota(limit, players.data(), leaders.data(), players.size(), localSlot);
}

void everyoneApartGetsAnEqualShare() {
    const std::vector<population_rules::Player> apart = {{0, at(0)}, {1, at(1000)}, {2, at(2000)}};
    check(quotaOf(apart, 0).quota == 4 && quotaOf(apart, 1).quota == 4 && quotaOf(apart, 2).quota == 4, "3 players apart: 4, 4, 4 of 12");
    check(quotaOf(apart, 1).leads == 1 && quotaOf(apart, 1).players == 3 && quotaOf(apart, 1).led[0] == 1, "each leads itself, of 3 players");
}

void aClusterIsLedByItsLowestSlot() {
    const std::vector<population_rules::Player> hostWithP2 = {{0, at(0)}, {1, at(50)}, {2, at(2000)}};
    const population_rules::Quota host = quotaOf(hostWithP2, 0);
    check(host.quota == 8 && host.leads == 2 && host.led[0] == 0 && host.led[1] == 1, "the host with P2 near and P3 apart: 8 of 12 for 2 of 3 players");
    check(quotaOf(hostWithP2, 1).quota == 0 && quotaOf(hostWithP2, 1).leads == 0, "P2 is led by the host: no quota of its own");
    check(quotaOf(hostWithP2, 2).quota == 4, "P3 apart leads itself: 4");
    const std::vector<population_rules::Player> guestsTogether = {{0, at(0)}, {1, at(2000)}, {2, at(2050)}};
    check(quotaOf(guestsTogether, 1).quota == 8 && quotaOf(guestsTogether, 2).quota == 0, "a guest-led cluster far from the host: the lower guest leads it");
    const std::vector<population_rules::Player> allTogether = {{0, at(0)}, {1, at(30)}, {2, at(60)}};
    check(quotaOf(allTogether, 0).quota == 12 && quotaOf(allTogether, 0).leads == 3, "all together: the host owns the whole cap, 3 of 3");
}

void theRemainderGoesToTheLowestLeaders() {
    const std::vector<population_rules::Player> five = {{0, at(0)}, {1, at(1000)}, {2, at(2000)}, {3, at(3000)}, {4, at(4000)}};
    int32_t sum = 0;
    for (uint8_t slot = 0; slot < 5; ++slot) sum += quotaOf(five, slot).quota;
    check(quotaOf(five, 0).quota == 3 && quotaOf(five, 1).quota == 3 && quotaOf(five, 2).quota == 2 && quotaOf(five, 4).quota == 2,
          "5 apart: floor(12/5) = 2 each and the remaining 2 to slots 0 and 1");
    check(sum == kClass1Limit, "the shares always add up to the cap");
    const std::vector<population_rules::Player> four = {{1, at(0)}, {2, at(1000)}, {5, at(2000)}, {7, at(3000)}};
    check(quotaOf(four, 1, 6 * 4).quota == 6 && quotaOf(four, 7, 6 * 4).quota == 6, "4 players with the cap grown to 6 per player: 6 each");
    check(quotaOf(four, 1, 10).quota == 3 && quotaOf(four, 2, 10).quota == 3 && quotaOf(four, 5, 10).quota == 2 && quotaOf(four, 7, 10).quota == 2,
          "a cap of 10 for 4 players: 3, 3, 2, 2 in slot order (slots 1, 2, 5, 7)");
    const std::vector<population_rules::Player> alone = {{0, at(0)}};
    check(quotaOf(alone, 0).quota == kClass1Limit, "a single player has the whole cap (nothing to share)");
    check(quotaOf({}, 0).quota == kClass1Limit && quotaOf({}, 0).players == 0, "no player known: the cap, not a zero quota");
}

void ownedTrafficBindsTheHeadroom() {
    check(population_rules::vehicleHeadroom(kShippedPool, 10, 2, 4, 4) == 0, "2 remote members, owned = quota: nothing more may spawn");
    check(population_rules::vehicleHeadroom(kShippedPool, 10, 2, 4, 6) == -2, "owned above the quota (clusters changed): negative, the caller compares");
    check(population_rules::vehicleHeadroom(kShippedPool, 10, 2, 8, 3) == 5 && population_rules::vehicleHeadroom(kShippedPool, 10, 2, 8, 0) == 8,
          "owned below the quota: what is left of it");
    check(population_rules::vehicleHeadroom(kShippedPool, 26, 2, 8, 0) == -2, "the pool rule binds when it is smaller (32 - 26 - 8)");
    check(population_rules::vehicleHeadroom(kGrownPool, 40, 3, 8, 2) == 6 && population_rules::vehicleHeadroom(kGrownPool, 40, 3, 20, 2) == 12,
          "the 64 pool: the smaller of 64 - 12 - in use and the quota left");
}

void theTrafficCapCanPassTheBase() {
    check(population_rules::budgetUncapped(12, 3.0f) == 24 && population_rules::budgetUncapped(12, 1.0f) == 12 && population_rules::budgetUncapped(12, 0.0f) == 6,
          "the uncapped traffic cap (base 12): 6 per area, 24 for 4 areas apart");
    check(population_rules::budget(12, 3.0f) == 12, "the capped budget stays at the base");
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
    oneRemoteIsTheShippedHeadroomAtEveryCount();
    everyoneApartGetsAnEqualShare();
    aClusterIsLedByItsLowestSlot();
    theRemainderGoesToTheLowestLeaders();
    ownedTrafficBindsTheHeadroom();
    theTrafficCapCanPassTheBase();
    areaCountForTheCensus();
    return g_failures == 0 ? 0 : 1;
}
