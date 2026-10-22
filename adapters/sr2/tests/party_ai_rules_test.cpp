// The homies, followers and NPC AI rules of milestone 16 wave 2 (no game): the shipped path, the share of a leader's extra followers,
// the record team, the players the team odds count, the odds, and the trace texts.
#include <cstdio>
#include <string>
#include <vector>

#include "../src/party_ai_rules.h"
#include "check.h"

namespace {

using namespace party_ai_rules;

constexpr uintptr_t kLocal = 0x1000;
constexpr uintptr_t kRemoteA = 0x2000;
constexpr uintptr_t kRemoteB = 0x3000;
constexpr uintptr_t kRemoteC = 0x4000;
constexpr uint8_t kNoSlot = partner_rules::kNoSlot;

std::vector<uintptr_t> itemsOf(const HumanList& list) { return {list.items, list.items + list.count}; }

partner_rules::Pairing pairing(bool trusted, std::initializer_list<partner_rules::Remote> remotes) {
    partner_rules::Pairing result{};
    result.trusted = trusted;
    for (const partner_rules::Remote& remote : remotes) result.items[result.count++] = remote;
    return result;
}

void shippedPathTests() {
    check(shippedPath(true, 0) && shippedPath(true, 1), "shippedPath: no remote and one remote take the shipped path");
    check(!shippedPath(true, 2) && !shippedPath(true, 3), "shippedPath: two or more remotes do not");
    check(shippedPath(false, 3) && shippedPath(false, 2), "shippedPath: an untrusted pairing always does");
}

void pairingTests() {
    const partner_rules::Pairing three = pairing(true, {{kRemoteA, 0x10, 1, 0}, {0, 0x20, 2, 0}, {kRemoteB, 0x30, 3, 0}});
    check(itemsOf(remoteHumansOf(three)) == std::vector<uintptr_t>{kRemoteA, kRemoteB}, "remoteHumansOf: a joining member has no human and is left out");
    check(remoteHumansOf(pairing(true, {})).count == 0, "remoteHumansOf: nothing for no remote");
    check(slotOfHuman(three, kRemoteB) == 3 && slotOfHuman(three, kRemoteA) == 1, "slotOfHuman: the slot of the owner");
    check(slotOfHuman(three, kLocal) == kNoSlot && slotOfHuman(three, 0) == kNoSlot, "slotOfHuman: kNoSlot for any other human");
}

void secondLeaderRankTests() {
    bool alwaysZero = true;
    for (uint32_t followers = 2; followers < 8; ++followers) alwaysZero = alwaysZero && secondLeaderRank(followers, 1) == 0;
    check(alwaysZero, "secondLeaderRank: one candidate takes every extra");
    const size_t expected[] = {0, 1, 2, 0, 1};
    bool goesRound = true;
    for (uint32_t followers = 2; followers <= 6; ++followers) goesRound = goesRound && secondLeaderRank(followers, 3) == expected[followers - 2];
    check(goesRound, "secondLeaderRank: three candidates and followers 2 to 6 give 0, 1, 2, 0, 1");
    check(secondLeaderRank(1, 3) == 0 && secondLeaderRank(0, 3) == 0 && secondLeaderRank(5, 0) == 0,
          "secondLeaderRank: below the first extra, or without candidates, 0");
}

void recordTeamTests() {
    check(recordTeam(0x0F) == kAnyTeam && recordTeam(0x0F) == -1, "recordTeam: 0x0F is -1, any team");
    check(recordTeam(0xF7) == 7 && recordTeam(0x07) == 7, "recordTeam: only the low nibble counts");
    check(recordTeam(0x08) == -8, "recordTeam: 0x08 is -8");
    check(recordTeam(0x30) == 0 && recordTeam(0x00) == 0, "recordTeam: a clear low nibble is team 0");
}

void othersTests() {
    const uintptr_t remotes[] = {kRemoteA, kRemoteB, kRemoteC};
    check(itemsOf(othersOf(kLocal, kLocal, remotes)) == std::vector<uintptr_t>{kRemoteA, kRemoteB, kRemoteC},
          "othersOf: the head is the local player, the others are the remotes in slot order");
    check(itemsOf(othersOf(kRemoteB, kLocal, remotes)) == std::vector<uintptr_t>{kLocal, kRemoteA, kRemoteC},
          "othersOf: the head is a remote, the local player comes first and the head is left out");
    const uintptr_t one[] = {kRemoteA};
    check(itemsOf(othersOf(kLocal, kLocal, one)) == std::vector<uintptr_t>{kRemoteA} &&
              itemsOf(othersOf(kRemoteA, kLocal, one)) == std::vector<uintptr_t>{kLocal},
          "othersOf: with one remote it is the single other of the shipped odds");
    check(othersOf(kLocal, 0, remotes).count == 3 && othersOf(kLocal, kLocal, std::span<const uintptr_t>()).count == 0,
          "othersOf: no local player, no remote");
    check(shippedOther(kLocal, kLocal, kRemoteA) == kRemoteA && shippedOther(kRemoteB, kLocal, kRemoteA) == kLocal,
          "shippedOther: the default partner for the local head, else the local player");
}

void teamTests() {
    const OtherTeam nearWithTwo[] = {{true, 2}};
    check(teamSize(1, nearWithTwo) == 1 + 1 + 1 + 2, "teamSize: one near other with two followers, the shipped formula");
    const OtherTeam far[] = {{false, 0}};
    check(teamSize(1, far) == 2 && teamSize(0, {}) == 1, "teamSize: a far other adds nothing, nobody else is the head alone");
    const OtherTeam mixed[] = {{true, 1}, {false, 3}, {true, 0}};
    check(teamSize(2, mixed) == 1 + 2 + (1 + 1) + (1 + 0), "teamSize: three others, mixed");
}

void enemyTests() {
    bool table = true;
    for (int bits = 0; bits < 16; ++bits) {
        const bool head = (bits & 1) != 0;
        const bool other = (bits & 2) != 0;
        const bool hostile = (bits & 4) != 0;
        const bool armed = (bits & 8) != 0;
        table = table && countsAsEnemy(head, other, hostile, armed) == ((head || other) && hostile && armed);
    }
    check(table, "countsAsEnemy: the truth table");
    check(teamOdds(5, 3) == 2 && teamOdds(2, 2) == 0 && teamOdds(1, 4) == -3, "teamOdds: enemies minus team");
}

void textTests() {
    const AiCounters zeros;
    check(aiSummaryLine(zeros) ==
              "trace: npc ai: per minute: unseen 0 (0 by another remote), view 0 (0), range 0 (0), reactions 0 (0), spawn team 0 (0), "
              "damage team 0 (0), follower target 0 (0), odds 0 (0), behaviour 3 target 0 (scoped 0)",
          "aiSummaryLine: zeros");
    AiCounters mixed;
    mixed.unseen = {10, 2};
    mixed.view = {3, 1};
    mixed.range = {4, 0};
    mixed.reactions = {5, 5};
    mixed.spawnTeam = {6, 1};
    mixed.damageTeam = {7, 2};
    mixed.followerTarget = {8, 3};
    mixed.odds = {9, 4};
    mixed.behaviour3 = {11, 6};
    check(aiSummaryLine(mixed) ==
              "trace: npc ai: per minute: unseen 10 (2 by another remote), view 3 (1), range 4 (0), reactions 5 (5), spawn team 6 (1), "
              "damage team 7 (2), follower target 8 (3), odds 9 (4), behaviour 3 target 11 (scoped 6)",
          "aiSummaryLine: mixed counts");

    const uint8_t none[] = {0};
    const uint8_t one[] = {2};
    const uint8_t three[] = {1, 2, 3};
    check(slotListText(std::span<const uint8_t>()) == "none", "slotListText: none for no slot");
    check(slotListText(std::span<const uint8_t>(one, 1)) == "2" && slotListText(std::span<const uint8_t>(none, 1)) == "0",
          "slotListText: one slot");
    check(slotListText(three) == "1, 2, 3", "slotListText: three slots, a comma and a space");
}

}  // namespace

int main() {
    shippedPathTests();
    pairingTests();
    secondLeaderRankTests();
    recordTeamTests();
    othersTests();
    teamTests();
    enemyTests();
    textTests();
    return g_failures == 0 ? 0 : 1;
}
