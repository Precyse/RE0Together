// The traffic trace rules of milestone 15 B1 (no game): the nearest player of a vehicle, the owned vehicles far from their owner,
// the counts per owner and area, the lines, and the vehicle part of the census line.
#include <cstdio>
#include <string>

#include "../src/traffic_trace_rules.h"
#include "../src/vehicle_census_rules.h"

namespace {

using population_rules::Player;
using population_rules::Point;
using traffic_trace_rules::FarOwned;
using traffic_trace_rules::OwnerAreaCounts;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

Point at(float x) { return {x, 0, 0}; }

void nearestPlayerBucketing() {
    const Player players[] = {{0, at(0)}, {1, at(100)}, {2, at(500)}};
    uint8_t slot = 99;
    float distance = 0;
    check(traffic_trace_rules::nearestPlayer(at(30), players, 3, slot, distance) && slot == 0 && distance == 30, "a vehicle near the host is in the host's area");
    check(traffic_trace_rules::nearestPlayer(at(90), players, 3, slot, distance) && slot == 1 && distance == 10, "nearer to player 2: its area");
    check(traffic_trace_rules::nearestPlayer(at(480), players, 3, slot, distance) && slot == 2 && distance == 20, "and the third");
    check(traffic_trace_rules::nearestPlayer(at(50), players, 3, slot, distance) && slot == 0, "a tie goes to the lower slot (equal distance 50 to slots 0 and 1)");
    const Player reversed[] = {{3, at(100)}, {2, at(0)}};
    check(traffic_trace_rules::nearestPlayer(at(50), reversed, 2, slot, distance) && slot == 2, "a tie goes to the lower slot whatever the order");
    check(!traffic_trace_rules::nearestPlayer(at(0), nullptr, 0, slot, distance), "no player: no area");
}

void farOwnedVehicles() {
    const Player owner{0, at(0)};
    const Player others[] = {{1, at(400)}, {2, at(900)}};
    FarOwned found = traffic_trace_rules::farOwned(at(380), owner, others, 2);
    check(found.isFar && found.nearestSlot == 1 && found.distance == 20, "380 from its owner and 20 from player 2: far from its owner");
    check(!traffic_trace_rules::farOwned(at(120), owner, others, 2).isFar, "within 150 of the owner is never far");
    check(!traffic_trace_rules::farOwned(at(150), owner, others, 2).isFar, "exactly 150 is not far");
    check(!traffic_trace_rules::farOwned(at(-300), owner, others, 2).isFar, "far from the owner but nearer to nobody else: not counted");
    check(!traffic_trace_rules::farOwned(at(200), owner, nullptr, 0).isFar, "no other player: not counted");
    found = traffic_trace_rules::farOwned(at(200), owner, others, 2);
    check(!found.isFar, "200 from the owner and 200 from player 2: not nearer to the other, not counted");
}

void countsPerOwnerAndArea() {
    OwnerAreaCounts counts;
    check(counts.describe() == "none" && counts.total() == 0, "no event: none");
    counts.add(0, 0);
    counts.add(0, 0);
    counts.add(0, 1);
    counts.add(2, 2);
    check(counts.total() == 4 && counts.describe() == "by slot 0 near slot 0 2, by slot 0 near slot 1 1, by slot 2 near slot 2 1",
          "counted per owner slot and area slot, in slot order");
    counts.add(200, 200);
    check(counts.describe().find("by slot 7 near slot 7 1") != std::string::npos, "a slot past the 8 of a game for 8 players is counted with the last one");
    counts.clear();
    check(counts.total() == 0 && counts.describe() == "none", "cleared after a line");
}

void theLines() {
    OwnerAreaCounts spawned;
    OwnerAreaCounts despawned;
    spawned.add(0, 0);
    despawned.add(1, 1);
    check(traffic_trace_rules::trafficLine(spawned, despawned, 0, {}) ==
              "trace: traffic: spawned by slot 0 near slot 0 1; despawned by slot 1 near slot 1 1; owned far from owner 0",
          "the traffic line with no owned vehicle far from its owner");
    check(traffic_trace_rules::trafficLine(OwnerAreaCounts(), OwnerAreaCounts(), 2, {true, 1, 20.4f}) ==
              "trace: traffic: spawned none; despawned none; owned far from owner 2 (nearest other slot 1 at 20)",
          "and with two, naming the first");

    population_rules::Quota quota;
    quota.quota = 8;
    quota.leads = 2;
    quota.players = 3;
    quota.led[0] = 0;
    quota.led[1] = 1;
    check(traffic_trace_rules::quotaLine(quota, 12, 5) == "trace: traffic: quota 8 of 12, leads 2 of 3 (slots {0,1}), owned 5",
          "the quota line");
    population_rules::Quota other = quota;
    check(traffic_trace_rules::sameQuota(quota, 12, other, 12), "the same quota is not a change");
    other.quota = 4;
    check(!traffic_trace_rules::sameQuota(quota, 12, other, 12), "another quota is a change");
    other = quota;
    other.led[1] = 2;
    check(!traffic_trace_rules::sameQuota(quota, 12, other, 12), "another set of led players is a change");
    check(!traffic_trace_rules::sameQuota(quota, 12, quota, 24), "another cap is a change");
}

void theCensusText() {
    vehicle_census_rules::Census census;
    census.inUse = 44;
    census.capacity = 64;
    census.own = 20;
    census.byOwner[0] = 20;
    census.byOwner[1] = 12;
    census.byOwner[2] = 8;
    census.byOwner[3] = 4;
    census.ownerSlots = 4;
    census.traffic = 11;
    census.ownTraffic = 4;
    census.quota = 8;
    census.leads = 2;
    census.players = 3;
    census.class1Count = 10;
    census.class1Limit = 12;
    census.netRecords = 130;
    const char* names[] = {"T1", "T2", "T4", "T6", "T7", "T8", "CN", "CU"};
    for (size_t i = 0; i < vehicle_census_rules::kSideTables; ++i) census.sideTables[i] = {names[i], static_cast<uint32_t>(20 + i)};
    check(vehicle_census_rules::text(census) ==
              "vehicles in use 44/64 (own 20, by owner slot 0:20 1:12 2:8 3:4), traffic 11 (own 4, quota 8, leads 2 of 3, class 1 10/12), "
              "net records 130/256, side tables T1 20/64 T2 21/64 T4 22/64 T6 23/64 T7 24/64 T8 25/64 CN 26/64 CU 27/64",
          "the vehicle part of the census line");
}

}  // namespace

int main() {
    nearestPlayerBucketing();
    farOwnedVehicles();
    countsPerOwnerAndArea();
    theLines();
    theCensusText();
    return g_failures == 0 ? 0 : 1;
}
