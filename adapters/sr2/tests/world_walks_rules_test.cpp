// The pure rules of milestone 17 wave 3 (no game): the Ambulance pose of one human, the remotes that get a follower bar, the remote
// whose completion masks the host holds, and the trace texts.
#include <cstdio>
#include <cstring>

#include "../src/world_walks_rules.h"
#include "check.h"

namespace {

using partner_rules::Remote;
using world_walks_rules::CompletionSender;
using world_walks_rules::FollowerBars;
using world_walks_rules::kMaxBars;
using world_walks_rules::kNone;
using world_walks_rules::kNoPose;

constexpr int32_t kPose = 0x397;
constexpr int32_t kAction = 0x12;
constexpr uintptr_t kHostPartner = 0x1000;
constexpr uintptr_t kSecond = 0x2000;
constexpr uintptr_t kThird = 0x3000;
constexpr uintptr_t kFourth = 0x4000;
constexpr uintptr_t kLocalFollower = 0x5000;
constexpr uintptr_t kPartnerFollower = 0x6000;
constexpr uintptr_t kNoHuman = 0;
constexpr uintptr_t kMemberBase = 0x9000;

Remote remote(uintptr_t human, uint8_t slot) { return {human, kMemberBase + slot, slot, 0}; }

void theAmbulancePose() {
    check(world_walks_rules::ambulancePose(kPose, kNoPose) == kPose, "a stored pose holds while the human has no action");
    check(world_walks_rules::ambulancePose(kPose, kAction) == kNoPose, "a stored pose gives way to an action in progress");
    check(world_walks_rules::ambulancePose(kNoPose, kNoPose) == kNoPose, "no stored pose, no pose");
    check(world_walks_rules::ambulancePose(kNoPose, kAction) == kNoPose, "no stored pose and an action, no pose");
    check(kNoPose == -1, "no pose is -1, the value the game's global holds");
}

void theOrderedRemotes() {
    uintptr_t out[kMaxBars] = {};
    const Remote three[] = {remote(kHostPartner, 1), remote(kSecond, 2), remote(kThird, 3)};
    check(world_walks_rules::orderedRemotes(three, 3, 1, out) == 3 && out[0] == kSecond && out[1] == kHostPartner && out[2] == kThird,
          "the default in the middle comes first, the others follow in pairing order");
    check(world_walks_rules::orderedRemotes(three, 3, 0, out) == 3 && out[0] == kHostPartner && out[1] == kSecond && out[2] == kThird,
          "the default at the front keeps the pairing order");
    check(world_walks_rules::orderedRemotes(three, 3, kNone, out) == 3 && out[0] == kHostPartner && out[2] == kThird,
          "without a default the pairing order stands");
    check(world_walks_rules::orderedRemotes(three, 0, kNone, out) == 0, "no remotes, none");
    const Remote gap[] = {remote(kHostPartner, 1), remote(kNoHuman, 2), remote(kThird, 3)};
    check(world_walks_rules::orderedRemotes(gap, 3, 0, out) == 2 && out[0] == kHostPartner && out[1] == kThird,
          "a remote without a human is skipped");
    const Remote noDefaultHuman[] = {remote(kNoHuman, 1), remote(kSecond, 2)};
    check(world_walks_rules::orderedRemotes(noDefaultHuman, 2, 0, out) == 1 && out[0] == kSecond, "a default without a human is skipped too");
    const Remote four[] = {remote(kHostPartner, 1), remote(kSecond, 2), remote(kThird, 3), remote(kFourth, 4)};
    check(world_walks_rules::orderedRemotes(four, 4, 0, out) == kMaxBars && out[2] == kThird, "four remotes give three");
    check(world_walks_rules::orderedRemotes(four, 4, 3, out) == kMaxBars && out[0] == kFourth && out[1] == kHostPartner && out[2] == kSecond,
          "a default among four is kept in the three");
}

bool sameBars(const FollowerBars& a, const FollowerBars& b) {
    return a.partner == b.partner && a.bar[0] == b.bar[0] && a.bar[1] == b.bar[1] && a.bar[2] == b.bar[2];
}

void theFollowerBars() {
    const FollowerBars game{kHostPartner, {kLocalFollower, kHostPartner, kPartnerFollower}};
    const uintptr_t ordered[] = {kHostPartner, kSecond, kThird};
    check(sameBars(world_walks_rules::followerBars(game, ordered, 0), game), "no remote leaves the game's bars");
    check(sameBars(world_walks_rules::followerBars(game, ordered, 1), game), "one remote leaves the game's bars");
    const FollowerBars two = world_walks_rules::followerBars(game, ordered, 2);
    check(two.partner == kHostPartner && two.bar[1] == kHostPartner && two.bar[2] == kSecond && two.bar[0] == kLocalFollower,
          "two remotes: the partner in bar 1, the next remote in bar 2, bar 0 stays");
    const FollowerBars three = world_walks_rules::followerBars(game, ordered, 3);
    check(three.partner == kHostPartner && three.bar[1] == kHostPartner && three.bar[2] == kSecond && three.bar[0] == kThird,
          "three remotes: the third takes bar 0");
    const FollowerBars noPartner{kNoHuman, {kLocalFollower, kNoHuman, kNoHuman}};
    check(sameBars(world_walks_rules::followerBars(noPartner, ordered, 3), noPartner), "a game without a partner keeps its bars with three remotes");
}

void theCompletionSender() {
    const Remote remotes[] = {remote(kHostPartner, 1), remote(kSecond, 2), remote(kThird, 3)};
    check(world_walks_rules::completionSenderIndex(remotes, 3, CompletionSender{3, kThird}) == 2, "the slot and human match");
    check(world_walks_rules::completionSenderIndex(remotes, 3, CompletionSender{2, kFourth}) == kNone, "the same slot with a new human is gone");
    check(world_walks_rules::completionSenderIndex(remotes, 3, CompletionSender{5, kThird}) == kNone, "the human in another slot is gone");
    check(world_walks_rules::completionSenderIndex(remotes, 2, CompletionSender{3, kThird}) == kNone, "a slot that left is gone");
    check(world_walks_rules::completionSenderIndex(remotes, 3, CompletionSender{}) == kNone, "none remembered, none found");
    const Remote joining[] = {remote(kNoHuman, world_walks_rules::kNoSlot)};
    check(world_walks_rules::completionSenderIndex(joining, 1, CompletionSender{}) == kNone, "none remembered never matches a remote without a slot");
}

void theTexts() {
    char text[world_walks_rules::kTextBytes];
    const FollowerBars bars{kHostPartner, {kNoHuman, kHostPartner, kSecond}};
    const char* shown = world_walks_rules::barsText(bars, text);
    check(shown == text && std::strcmp(text, "0x0 0x1000 0x2000") == 0, "the bars show 0 as 0x0");
    world_walks_rules::slotText(2, kThird, text);
    check(std::strcmp(text, "slot 2 human 0x3000") == 0, "the slot text names the slot and the human");
    world_walks_rules::slotText(world_walks_rules::kNoSlot, kNoHuman, text);
    check(text[0] != '\0' && std::strstr(text, "0x0") != nullptr, "an empty subject still gives text");
}

}  // namespace

int main() {
    theAmbulancePose();
    theOrderedRemotes();
    theFollowerBars();
    theCompletionSender();
    theTexts();
    return g_failures == 0 ? 0 : 1;
}
