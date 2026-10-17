// The pure partner rules of milestone 4 (no game): the human to member mapping, the default partner every machine
// agrees on, the join and leave rules and the nearest remote.
#include <cstdio>

#include "../src/partner_rules.h"

namespace {

using partner_rules::HumanEntry;
using partner_rules::kNone;
using partner_rules::MemberEntry;
using partner_rules::Pairing;
using partner_rules::Vec3;

constexpr uint32_t kInGame = sr2::kMemberStateInGame;
constexpr uint32_t kJoining = 7;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

Pairing paired(const HumanEntry* humans, size_t humanCount, const MemberEntry* members, size_t memberCount) {
    Pairing pairing;
    partner_rules::pair(humans, humanCount, members, memberCount, pairing);
    return pairing;
}

// Three remotes whose list order is not their slot order.
void pairingSortsBySlotAndJoinsByOwnerSlot() {
    const HumanEntry humans[] = {{0xA3, 3}, {0xA1, 1}, {0xA2, 2}};
    const MemberEntry members[] = {{0xB2, 2, kInGame}, {0xB3, 3, kInGame}, {0xB1, 1, kInGame}};
    const Pairing pairing = paired(humans, 3, members, 3);
    check(pairing.trusted && pairing.count == 3, "three remotes pair up");
    check(pairing.items[0].slot == 1 && pairing.items[1].slot == 2 && pairing.items[2].slot == 3,
          "items are in ascending slot order");
    check(pairing.items[0].human == 0xA1 && pairing.items[0].member == 0xB1 && pairing.items[2].human == 0xA3 &&
              pairing.items[2].member == 0xB3,
          "each member gets the human with its slot");
}

void joiningMemberHasNoHumanYet() {
    const HumanEntry humans[] = {{0xA1, 1}};
    const MemberEntry members[] = {{0xB1, 1, kInGame}, {0xB2, 2, kJoining}};
    const Pairing pairing = paired(humans, 1, members, 2);
    check(pairing.trusted && pairing.count == 2 && pairing.items[1].member == 0xB2 && pairing.items[1].human == 0,
          "a joining member is kept without a human");
}

void aHumanWithoutMemberFallsBackToTheFirstOfEachList() {
    const HumanEntry humans[] = {{0xA9, 7}, {0xA1, 1}};
    const MemberEntry members[] = {{0xB2, 2, kInGame}, {0xB1, 1, kInGame}};
    const Pairing pairing = paired(humans, 2, members, 2);
    check(!pairing.trusted && pairing.count == 1, "an owner slot that matches no member is not trusted");
    check(pairing.items[0].human == 0xA9 && pairing.items[0].member == 0xB2,
          "the fallback is the first remote human and the first remote member, as the game picks");
    const HumanEntry noSlot[] = {{0xA1, partner_rules::kNoSlot}};
    check(!paired(noSlot, 1, members, 2).trusted, "a human without a net record is not trusted");
    const HumanEntry twins[] = {{0xA1, 1}, {0xA2, 1}};
    check(!paired(twins, 2, members, 2).trusted, "two humans with one slot are not trusted");
}

void emptyLists() {
    const Pairing pairing = paired(nullptr, 0, nullptr, 0);
    check(pairing.trusted && pairing.count == 0, "no remote at all is an empty, trusted pairing");
    const MemberEntry members[] = {{0xB1, 1, kJoining}};
    const Pairing joiningOnly = paired(nullptr, 0, members, 1);
    check(joiningOnly.trusted && joiningOnly.count == 1 && joiningOnly.items[0].human == 0,
          "a member without any human yet is kept");
}

void defaultPartnerAgreesOnEveryMachine() {
    const HumanEntry humans[] = {{0xA3, 3}, {0xA1, 1}, {0xA2, 2}};
    const MemberEntry members[] = {{0xB3, 3, kInGame}, {0xB1, 1, kInGame}, {0xB2, 2, kInGame}};
    const Pairing pairing = paired(humans, 3, members, 3);
    const size_t onHost = partner_rules::pickDefault(pairing, 0, true);
    check(onHost != kNone && pairing.items[onHost].human == 0xA1, "the host picks the remote with the lowest slot");
    const size_t onGuest = partner_rules::pickDefault(pairing, 0xB2, false);
    check(onGuest != kNone && pairing.items[onGuest].human == 0xA2, "a guest picks the host's player");
    const size_t hostMissing = partner_rules::pickDefault(pairing, 0xBEEF, false);
    check(hostMissing != kNone && pairing.items[hostMissing].human == 0xA1, "a guest without a host member takes the lowest slot");

    const HumanEntry one[] = {{0xA2, 2}};
    const MemberEntry oneMember[] = {{0xB2, 2, kInGame}};
    const Pairing single = paired(one, 1, oneMember, 1);
    check(single.items[partner_rules::pickDefault(single, 0xB2, false)].human == 0xA2 &&
              single.items[partner_rules::pickDefault(single, 0, true)].human == 0xA2,
          "with one remote the default is that remote on both sides, as in the shipped game");

    const Pairing nobody = paired(nullptr, 0, nullptr, 0);
    check(partner_rules::pickDefault(nobody, 0, true) == kNone, "no remote, no default");
    const MemberEntry joining[] = {{0xB1, 1, kJoining}};
    check(partner_rules::pickDefault(paired(nullptr, 0, joining, 1), 0, true) == kNone,
          "a member without a human is never the default human");
}

void readyWhenEveryRemoteMemberHasAReadyHuman() {
    const HumanEntry humans[] = {{0xA1, 1}, {0xA2, 2}};
    const MemberEntry members[] = {{0xB1, 1, kInGame}, {0xB2, 2, kInGame}};
    const Pairing pairing = paired(humans, 2, members, 2);
    check(partner_rules::allReady(pairing, [](uintptr_t) { return true; }), "all humans ready");
    check(!partner_rules::allReady(pairing, [](uintptr_t human) { return human != 0xA2; }), "one human still joining");
    const MemberEntry moreMembers[] = {{0xB1, 1, kInGame}, {0xB2, 2, kInGame}, {0xB3, 3, kJoining}};
    check(!partner_rules::allReady(paired(humans, 2, moreMembers, 3), [](uintptr_t) { return true; }),
          "a member whose human does not exist yet is not ready");
    check(partner_rules::allReady(paired(nullptr, 0, nullptr, 0), [](uintptr_t) { return false; }), "no remote member is ready");
}

void firstJoiningIsTheFirstBelowInGame() {
    const MemberEntry members[] = {{0xB1, 1, kInGame}, {0xB2, 2, kJoining}, {0xB3, 3, 3}};
    const Pairing pairing = paired(nullptr, 0, members, 3);
    const size_t joining = partner_rules::firstJoining(pairing, kInGame);
    check(joining != kNone && pairing.items[joining].member == 0xB2, "the lowest joining slot is named");
    const MemberEntry settled[] = {{0xB1, 1, kInGame}};
    check(partner_rules::firstJoining(paired(nullptr, 0, settled, 1), kInGame) == kNone, "nobody joining");
}

void lastInGameEndsTheSession() {
    const MemberEntry two[] = {{0xB1, 1, kInGame}, {0xB2, 2, kInGame}};
    check(!partner_rules::isLastInGame(paired(nullptr, 0, two, 2), 0xB1, kInGame), "another remote is in the game");
    const MemberEntry one[] = {{0xB1, 1, kInGame}};
    check(partner_rules::isLastInGame(paired(nullptr, 0, one, 1), 0xB1, kInGame), "the only remote left: the session ends");
    const MemberEntry joiner[] = {{0xB1, 1, kInGame}, {0xB2, 2, kJoining}};
    check(partner_rules::isLastInGame(paired(nullptr, 0, joiner, 2), 0xB1, kInGame), "a member still joining does not keep it");
}

void nearestRemote() {
    const Vec3 positions[] = {{100, 0, 0}, {10, 0, 0}, {10, 0, 0}, {0, 50, 0}};
    check(partner_rules::nearestIndex(positions, 4, {0, 0, 0}) == 1, "closest wins, the first of equals");
    check(partner_rules::nearestIndex(positions, 1, {99, 0, 0}) == 0, "one position");
    check(partner_rules::nearestIndex(positions, 0, {0, 0, 0}) == kNone, "no position");
    const Vec3 heights[] = {{0, 0, 30}, {0, 0, -20}};
    check(partner_rules::nearestIndex(heights, 2, {0, 0, 0}) == 1, "height counts");
}

void othersAllDown() {
    const HumanEntry humans[] = {{0xA1, 1}, {0xA2, 2}, {0xA3, 3}};
    const MemberEntry members[] = {{0xB1, 1, kInGame}, {0xB2, 2, kInGame}, {0xB3, 3, kInGame}};
    const Pairing pairing = paired(humans, 3, members, 3);
    check(partner_rules::othersAllDown(pairing, 0xA1, [](uintptr_t human) { return human != 0xA1; }), "all others down");
    check(!partner_rules::othersAllDown(pairing, 0xA1, [](uintptr_t human) { return human == 0xA2; }), "one other is up");
    const MemberEntry single[] = {{0xB1, 1, kInGame}};
    const HumanEntry onlyHuman[] = {{0xA1, 1}};
    check(partner_rules::othersAllDown(paired(onlyHuman, 1, single, 1), 0xA1, [](uintptr_t) { return false; }),
          "nobody else: nothing is up");
}

void playerPool() {
    constexpr uintptr_t kFirst = sr2::kPlayerPool;
    constexpr uintptr_t kPast = sr2::kPlayerPool + sr2::kPlayerPoolCount * sr2::kPlayerPoolStride;
    check(partner_rules::isPlayerPoolMember(kFirst) && partner_rules::isPlayerPoolMember(kFirst + sr2::kPlayerPoolStride),
          "a human in the player pool is a player");
    check(partner_rules::isPlayerPoolMember(kPast - 1), "the last byte of the pool is still in it");
    check(!partner_rules::isPlayerPoolMember(kFirst - 1) && !partner_rules::isPlayerPoolMember(kPast) && !partner_rules::isPlayerPoolMember(0),
          "an address outside the pool, and none, is an NPC");
}

}  // namespace

int main() {
    pairingSortsBySlotAndJoinsByOwnerSlot();
    joiningMemberHasNoHumanYet();
    aHumanWithoutMemberFallsBackToTheFirstOfEachList();
    emptyLists();
    defaultPartnerAgreesOnEveryMachine();
    readyWhenEveryRemoteMemberHasAReadyHuman();
    firstJoiningIsTheFirstBelowInGame();
    lastInGameEndsTheSession();
    nearestRemote();
    othersAllDown();
    playerPool();
    return g_failures == 0 ? 0 : 1;
}
