// The pure rules of milestone 17 wave 3, Crowd Control (no game): the nav offsets of an event, the holder of the celebrity, the
// warp rule and the order of the protect start.
#include <cstdio>
#include <iterator>

#include "../src/crowd_control_rules.h"

namespace {

using crowd_control_rules::kNone;
using crowd_control_rules::NavSide;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

constexpr uint32_t kEventStrideWords = 0x5B2;  // the game's imul constant: the event record is this many dwords
constexpr uint32_t kThirdEvent = 3;
constexpr uintptr_t kLocalHuman = 0x1000;
constexpr uintptr_t kCelebrity = 0x2000;
constexpr uintptr_t kRemoteA = 0x3000;
constexpr uintptr_t kRemoteB = 0x4000;
constexpr uintptr_t kRemoteC = 0x5000;

void theNavOffsets() {
    check(crowd_control_rules::navOffset(0, NavSide::Local) == 0x60 && crowd_control_rules::navOffset(0, NavSide::Remote) == 0x64 &&
              crowd_control_rules::navOffset(0, NavSide::End) == 0x68,
          "event 0 keeps its navs at 0x60, 0x64 and 0x68");
    check(crowd_control_rules::navOffset(kThirdEvent, NavSide::Remote) == kThirdEvent * 0x16C8 + 0x64, "event 3 remote is 3 * 0x16C8 + 0x64");
    bool matchesTheGame = true;
    for (uint32_t event = 0; event < 8; ++event) {
        for (uint32_t i = 0; i < 2; ++i) {
            const uintptr_t game = (event * kEventStrideWords + (i & 1)) * sizeof(uint32_t) + 0x60;
            matchesTheGame = matchesTheGame && crowd_control_rules::navOffset(event, i == 0 ? NavSide::Local : NavSide::Remote) == game;
        }
    }
    check(matchesTheGame, "the offsets equal the game's (event * 0x5B2 + (i & 1)) * 4 + 0x60 for i = 0 and 1");
}

void theHolder() {
    const uint32_t held[] = {0, 0x77, 0x88, 0x77};
    check(crowd_control_rules::holderIndex(held, 4, 0) == kNone, "a handle of 0 has no holder, although some remotes hold nothing");
    check(crowd_control_rules::holderIndex(held, 4, 0x99) == kNone, "a handle nobody holds has no holder");
    check(crowd_control_rules::holderIndex(held, 4, 0x88) == 2, "one match gives its index");
    check(crowd_control_rules::holderIndex(held, 4, 0x77) == 1, "the first in order wins when two entries show the same handle");
    check(crowd_control_rules::holderIndex(held, 0, 0x77) == kNone, "no remote has no holder");
}

void theWarp() {
    check(crowd_control_rules::warpsEveryRemote(true), "a remote player human warps every remote");
    check(!crowd_control_rules::warpsEveryRemote(false), "the local player, no human and an NPC keep the game's call");
}

void theProtectOrder() {
    uintptr_t order[crowd_control_rules::kMaxProtectOrder];
    const uintptr_t one[] = {kRemoteA};
    check(crowd_control_rules::protectStartOrder(kLocalHuman, one, 1, kCelebrity, order) == 3 && order[0] == kLocalHuman &&
              order[1] == kRemoteA && order[2] == kCelebrity,
          "one remote gives the shipped three: local, remote, celebrity");
    const uintptr_t three[] = {kRemoteA, kRemoteB, kRemoteC};
    check(crowd_control_rules::protectStartOrder(kLocalHuman, three, 3, kCelebrity, order) == 5 && order[1] == kRemoteA && order[2] == kRemoteB &&
              order[3] == kRemoteC && order[4] == kCelebrity,
          "three remotes are walked between the local player and the celebrity, in order");
    check(crowd_control_rules::protectStartOrder(kLocalHuman, nullptr, 0, kCelebrity, order) == 2 && order[0] == kLocalHuman && order[1] == kCelebrity,
          "no remote gives {local, celebrity}");
    const uintptr_t withZero[] = {0, kRemoteB};
    check(crowd_control_rules::protectStartOrder(0, withZero, 2, 0, order) == 1 && order[0] == kRemoteB, "zeros are skipped");
    uintptr_t many[crowd_control_rules::kMaxProtectOrder + 2];
    for (uintptr_t& entry : many) entry = kRemoteA;
    check(crowd_control_rules::protectStartOrder(kLocalHuman, many, std::size(many), kCelebrity, order) == crowd_control_rules::kMaxProtectOrder &&
              order[crowd_control_rules::kMaxProtectOrder - 1] == kCelebrity,
          "a list longer than the table is cut before the celebrity");
}

}  // namespace

int main() {
    theNavOffsets();
    theHolder();
    theWarp();
    theProtectOrder();
    return g_failures == 0 ? 0 : 1;
}
