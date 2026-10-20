// The pure rules of milestone 16 W2 mission flow (no game): send targets, the prompt that opened, the accept decision, the remote a
// restart keeps, the replay answer, the decline key, the joined names and the bonus flag.
#include <cstdio>
#include <string>
#include <string_view>

#include "../src/activity_rules.h"
#include "../src/mission_flow_rules.h"

namespace {

namespace rules = mission_flow_rules;

constexpr uintptr_t kHost = 0x100;
constexpr uintptr_t kSecond = 0x200;
constexpr uintptr_t kThird = 0x300;
constexpr uintptr_t kLocal = 0xA000;
constexpr uintptr_t kSecondHuman = 0xB000;
constexpr uintptr_t kThirdHuman = 0xC000;
constexpr uintptr_t kFallback = 0xD000;
constexpr uint32_t kNoMission = 0x77;
constexpr uint32_t kMission = 0x55;
constexpr size_t kOneRemote = 1;
constexpr size_t kTwoRemotes = 2;
constexpr size_t kNameCapacity = 128;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

rules::SendInputs inputs(bool isHost, size_t remotes, uintptr_t explicitMember, uintptr_t scoped, uintptr_t remembered) {
    return {isHost, remotes, explicitMember, scoped, remembered, kHost};
}

bool broadcasts(const rules::Target& target) { return target.kind == rules::TargetKind::Broadcast; }

bool goesTo(const rules::Target& target, uintptr_t member) { return target.kind == rules::TargetKind::Member && target.member == member; }

void theSendTarget() {
    check(broadcasts(rules::sendTarget(inputs(true, kOneRemote, kSecond, kThird, kSecond))) &&
              broadcasts(rules::sendTarget(inputs(false, kOneRemote, 0, 0, 0))) && broadcasts(rules::sendTarget(inputs(true, 0, 0, 0, 0))),
          "one remote is always the game's broadcast, whatever else is known");
    check(goesTo(rules::sendTarget(inputs(false, kTwoRemotes, 0, kThird, 0)), kHost), "a guest sends to the host, even inside a scope");
    check(broadcasts(rules::sendTarget({false, kTwoRemotes, 0, 0, 0, 0})), "a guest without a host member keeps the broadcast");
    check(goesTo(rules::sendTarget(inputs(true, kTwoRemotes, kSecond, kThird, kThird)), kSecond), "on the host an explicit member beats the scope");
    check(goesTo(rules::sendTarget(inputs(true, kTwoRemotes, 0, kThird, kSecond)), kThird), "on the host the scoped sender beats the remembered requester");
    check(goesTo(rules::sendTarget(inputs(true, kTwoRemotes, 0, 0, kSecond)), kSecond), "on the host the remembered requester answers outside a scope");
    check(broadcasts(rules::sendTarget(inputs(true, kTwoRemotes, 0, 0, 0))), "on the host nothing known means every member");
}

void thePromptAndAccept() {
    check(rules::promptOpened({0, false}, {5, false}), "a new dialog is an opened prompt");
    check(!rules::promptOpened({5, false}, {5, false}), "the same dialog is not");
    check(!rules::promptOpened({5, false}, {0, false}), "a dialog that was cleared is not (the demolition derby path)");
    check(rules::promptOpened({0, false}, {0, true}), "a prompt put off until later is");
    check(!rules::promptOpened({0, true}, {0, true}), "a prompt that was already put off is not");

    check(rules::acceptDecision(kNoMission, kNoMission, false, kTwoRemotes) == rules::AcceptDecision::Skip, "no mission pending: nothing to start");
    check(rules::acceptDecision(kMission, kNoMission, true, kTwoRemotes) == rules::AcceptDecision::SkipLate,
          "with 2 remotes an accept for a mission already set up is dropped");
    check(rules::acceptDecision(kMission, kNoMission, false, kTwoRemotes) == rules::AcceptDecision::Start, "the first accept starts");
    check(rules::acceptDecision(kMission, kNoMission, true, kOneRemote) == rules::AcceptDecision::Start,
          "with one remote a mission already set up still starts (the shipped path)");
}

void thePairRemote() {
    const auto isRemote = [](uintptr_t human) { return human == kSecondHuman || human == kThirdHuman; };
    check(rules::pairRemote(kLocal, kThirdHuman, kLocal, isRemote, kFallback) == kThirdHuman, "owner local, partner remote: the partner");
    check(rules::pairRemote(kThirdHuman, kLocal, kLocal, isRemote, kFallback) == kThirdHuman, "owner remote, partner local: the owner");
    check(rules::pairRemote(0, 0, kLocal, isRemote, kFallback) == kFallback, "a pair of dead handles gives the fallback");
    check(rules::pairRemote(kLocal, 0xE000, kLocal, isRemote, kFallback) == kFallback, "a partner that is no remote player gives the fallback");
    check(rules::pairRemote(kLocal, kSecondHuman, kLocal, isRemote, kSecondHuman) == kSecondHuman, "with one remote the pair's remote is the fallback");
}

void theReplayAndKeys() {
    check(rules::replayAnswerApplies(sr2::kReplayAnswerAccept, false), "an accept always applies");
    check(!rules::replayAnswerApplies(0, false) && rules::replayAnswerApplies(0, true), "a decline applies only when every remote declined");
    check(rules::answersPrompt(1) && rules::answersPrompt(2) && !rules::answersPrompt(0) && !rules::answersPrompt(3),
          "the prompt answers are the events 1 and 2");
    check(rules::declineKey(0x40, kMission) == 0x40 && rules::declineKey(0, kMission) == kMission, "the wait dialog is the key, else the pending id");

    activity_rules::DeclineTally tally;
    const uintptr_t both[] = {kSecond, kThird};
    check(!tally.declinedAll(rules::declineKey(1, kMission), kSecond, both, 2), "the first decline of two does not end the ask");
    check(!tally.declinedAll(rules::declineKey(2, kMission), kThird, both, 2) && tally.declinedCount() == 1,
          "a new wait dialog for the same mission starts the tally over");
}

// The leave rule as mission_flow::memberLeft applies it: the leaver is dropped from the required set and counted as a decline.
bool leaveCancels(activity_rules::DeclineTally& tally, uint32_t key, uintptr_t leaver, const uintptr_t* inGame, size_t count) {
    uintptr_t remaining[activity_rules::kMaxMembers];
    const size_t stay = rules::withoutMember(inGame, count, leaver, remaining);
    return rules::replayLeaveCancels(stay, tally.declinedAll(key, leaver, remaining, stay));
}

void theReplayLeaveRule() {
    uintptr_t out[activity_rules::kMaxMembers];
    const uintptr_t three[] = {kSecond, kThird, kHost};
    check(rules::withoutMember(three, 3, kThird, out) == 2 && out[0] == kSecond && out[1] == kHost, "the leaver is dropped, the order kept");
    check(rules::withoutMember(three, 3, kLocal, out) == 3 && out[2] == kHost, "a leaver that is not listed drops nobody");
    const uintptr_t twice[] = {kSecond, kThird, kSecond};
    check(rules::withoutMember(twice, 3, kSecond, out) == 1 && out[0] == kThird, "a leaver listed twice is dropped both times");
    check(rules::withoutMember(three, 0, kSecond, out) == 0, "an empty list stays empty");
    check(rules::replayLeaveCancels(1, true) && !rules::replayLeaveCancels(1, false) && !rules::replayLeaveCancels(0, true),
          "a leave cancels only with remotes left that all declined");

    const uintptr_t both[] = {kSecond, kThird};
    const uint32_t dialog = rules::declineKey(1, kMission);
    activity_rules::DeclineTally tally;
    check(!tally.declinedAll(dialog, kSecond, both, 2), "the first of two declines is withheld");
    check(leaveCancels(tally, dialog, kThird, both, 2), "the other one leaving ends the wait");

    activity_rules::DeclineTally nobody;
    check(!leaveCancels(nobody, dialog, kThird, both, 2), "a leave with nobody declined only counts");
    check(nobody.declinedAll(dialog, kSecond, both, 2), "the remote that declines afterwards ends the wait");

    activity_rules::DeclineTally single;
    const uintptr_t one[] = {kSecond};
    check(!leaveCancels(single, dialog, kSecond, one, 1), "the only remote leaving is the game's own teardown");

    activity_rules::DeclineTally earlier;
    leaveCancels(earlier, dialog, kThird, both, 2);
    check(!earlier.declinedAll(rules::declineKey(2, kMission), kSecond, both, 2), "a leaver of an earlier wait does not count for a new one");
}

void theNamesAndFlags() {
    char16_t out[kNameCapacity];
    const std::u16string_view one[] = {u"Ana"};
    check(rules::joinNames(one, 1, out, kNameCapacity) == 3 && std::u16string_view(out) == u"Ana", "one name is that name");
    const std::u16string_view three[] = {u"Ana", u"Bo", u"Cy"};
    check(std::u16string_view(out, rules::joinNames(three, 3, out, kNameCapacity)) == u"Ana / Bo / Cy", "names are joined with a slash");
    const char16_t terminated[] = u"Di\0junk";
    const std::u16string_view cut[] = {std::u16string_view(terminated, sizeof(terminated) / sizeof(char16_t) - 1), u"Ed"};
    check(std::u16string_view(out, rules::joinNames(cut, 2, out, kNameCapacity)) == u"Di / Ed", "a name stops at its terminator");
    const std::u16string long_name(sr2::kNameChars + 10, u'x');
    const std::u16string_view longNames[] = {long_name};
    check(rules::joinNames(longNames, 1, out, kNameCapacity) == sr2::kNameChars, "a name stops after 0x40 characters");
    constexpr size_t kSmall = 6;
    const size_t written = rules::joinNames(three, 3, out, kSmall);
    check(written == kSmall - 1 && std::u16string_view(out, written) == u"Ana /" && out[written] == u'\0', "the capacity cuts the text and keeps the terminator");
    check(rules::joinNames(three, 3, out, 0) == 0, "no capacity writes nothing");

    check(rules::bonusFlagOffset(true) == 5 && rules::bonusFlagOffset(false) == 4, "the bonus flag follows the sub id of a 4 or 3 byte header");
    check(rules::payloadOffset(false, 4) == 7 && rules::payloadOffset(true, 4) == 8, "the reason of a start reply follows the mission id");
    check(rules::bonusFlag(kSecondHuman, kSecondHuman) == 1 && rules::bonusFlag(kLocal, kSecondHuman) == 0,
          "a bonus is flagged for the recipient it belongs to");
    check(rules::bonusFlag(kThirdHuman, kSecondHuman) == 0, "the bonus of a third player is not the second player's");
}

}  // namespace

int main() {
    theSendTarget();
    thePromptAndAccept();
    thePairRemote();
    theReplayAndKeys();
    theReplayLeaveRule();
    theNamesAndFlags();
    return g_failures == 0 ? 0 : 1;
}
