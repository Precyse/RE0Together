// Checks the host item ledger rules and the pickup hold (no game). Exit 0 when every check passes.
#include <cstdio>
#include <string>

#include "../src/item_ledger_rule.h"

namespace {

namespace rule = item_ledger_rule;
using rule::Answer;
using rule::Key;
using Step = rule::TakeHold::Step;

constexpr uint8_t kHost = 0;
constexpr uint8_t kGuest = 1;
constexpr uint16_t kScene = 0x0a05;
constexpr uint32_t kHerb = 0x2b;
constexpr uint32_t kAmmo = 0x14;
constexpr rule::Vec3 kSpot{100.0f, 0.0f, 200.0f};
constexpr uint32_t kStart = 1000;
int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

Key herbAt(rule::Vec3 pos) { return {kScene, kHerb, pos}; }

void testFirstRequestWinsInBothOrders() {
    rule::Ledger ledger;
    const rule::Decision first = ledger.request(herbAt(kSpot), kHost, kStart);
    const rule::Decision second = ledger.request(herbAt(kSpot), kGuest, kStart + 5);
    check(first.answer == Answer::Granted, "the first request is granted");
    check(second.answer == Answer::Claimed, "the second request is refused while the first holds the item");
    check(second.serial == first.serial, "the refusal names the grant that caused it");

    rule::Ledger reversed;
    check(reversed.request(herbAt(kSpot), kGuest, kStart).answer == Answer::Granted, "the guest first is granted");
    check(reversed.request(herbAt(kSpot), kHost, kStart + 5).answer == Answer::Claimed, "the host second is refused");
}

void testSerialsCount() {
    rule::Ledger ledger;
    const rule::Decision a = ledger.request({kScene, kHerb, kSpot}, kHost, kStart);
    const rule::Decision b = ledger.request({kScene, kAmmo, kSpot}, kHost, kStart);
    check(a.serial == 1 && b.serial == 2, "grants are numbered 1, 2, ...");
}

void testDistinctItems() {
    rule::Ledger ledger;
    check(ledger.request(herbAt(kSpot), kHost, kStart).answer == Answer::Granted, "first herb granted");
    check(ledger.request(herbAt({400.0f, 0.0f, 200.0f}), kGuest, kStart).answer == Answer::Granted,
          "a herb elsewhere is another item");
    check(ledger.request({kScene, kAmmo, kSpot}, kGuest, kStart).answer == Answer::Granted,
          "another id at the same spot is another item");
    check(ledger.request({0x0a06, kHerb, kSpot}, kGuest, kStart).answer == Answer::Granted,
          "another scene is another item");
    check(ledger.request(herbAt({104.0f, 0.0f, 203.0f}), kGuest, kStart).answer == Answer::Claimed,
          "a few units off is the same item");
}

void testHolderAsksAgain() {
    rule::Ledger ledger;
    const rule::Decision first = ledger.request(herbAt(kSpot), kGuest, kStart);
    const rule::Decision again = ledger.request(herbAt(kSpot), kGuest, kStart + 10000);
    check(again.answer == Answer::Granted && again.serial == first.serial,
          "the holder asking again is granted the same entry");
    check(ledger.request(herbAt(kSpot), kHost, kStart + 10000 + rule::kReservationMs - 1).answer == Answer::Claimed,
          "asking again renewed the reservation");
}

void testReservationExpires() {
    rule::Ledger ledger;
    check(ledger.request(herbAt(kSpot), kGuest, kStart).answer == Answer::Granted, "guest granted");
    check(ledger.request(herbAt(kSpot), kHost, kStart + rule::kReservationMs).answer == Answer::Claimed,
          "still held at the limit");
    const rule::Decision late = ledger.request(herbAt(kSpot), kHost, kStart + rule::kReservationMs + 1);
    check(late.answer == Answer::Granted, "a declined prompt frees the item once the reservation ran out");
    check(late.serial == 2, "the new holder gets a new serial");
    check(ledger.request(herbAt(kSpot), kGuest, kStart + rule::kReservationMs + 2).answer == Answer::Claimed,
          "the former holder is now refused");
}

void testTakenIsFinal() {
    rule::Ledger ledger;
    ledger.request(herbAt(kSpot), kGuest, kStart);
    ledger.taken(herbAt(kSpot));
    check(ledger.request(herbAt(kSpot), kHost, kStart + 10).answer == Answer::Taken, "a consumed item is refused as taken");
    check(ledger.request(herbAt(kSpot), kGuest, kStart + rule::kReservationMs * 3).answer == Answer::Taken,
          "a consumed item stays taken for its taker and after any time");
    check(ledger.size() == 1, "one entry for the item");
}

void testTakenWithoutGrant() {
    rule::Ledger ledger;
    ledger.taken(herbAt(kSpot));
    check(ledger.request(herbAt(kSpot), kHost, kStart).answer == Answer::Taken,
          "a take the ledger never granted is journaled");
}

void testPutForgets() {
    rule::Ledger ledger;
    ledger.request(herbAt(kSpot), kGuest, kStart);
    ledger.taken(herbAt(kSpot));
    ledger.put({kScene, kAmmo, kSpot});
    check(ledger.size() == 1, "a put of another id leaves the entry");
    ledger.put(herbAt(kSpot));
    check(ledger.size() == 0, "a put at the spot forgets the entry");
    check(ledger.request(herbAt(kSpot), kHost, kStart + 10).answer == Answer::Granted,
          "the dropped item can be taken again");
}

void testClearOnSave() {
    rule::Ledger ledger;
    ledger.request(herbAt(kSpot), kGuest, kStart);
    ledger.taken(herbAt({500.0f, 0.0f, 0.0f}));
    ledger.clear();
    check(ledger.size() == 0, "clear empties the journal");
    check(ledger.request(herbAt(kSpot), kHost, kStart).answer == Answer::Granted, "a cleared ledger grants again");
}

void testBounded() {
    rule::Ledger ledger;
    for (size_t index = 0; index < rule::kMaxEntries + 10; ++index) {
        ledger.request(herbAt({static_cast<float>(index) * 100.0f, 0.0f, 0.0f}), kHost, kStart);
    }
    check(ledger.size() == rule::kMaxEntries, "the ledger holds at most kMaxEntries");
    check(ledger.request(herbAt({0.0f, 0.0f, 0.0f}), kGuest, kStart).answer == Answer::Granted,
          "the oldest entry was dropped");
}

void testConsumedJournal() {
    rule::Ledger ledger;
    ledger.request(herbAt(kSpot), kGuest, kStart);
    check(!ledger.consumed(herbAt(kSpot)), "a grant is not a consumed item");
    ledger.taken(herbAt(kSpot));
    ledger.taken({kScene, kAmmo, {500.0f, 0.0f, 0.0f}});
    check(ledger.consumed(herbAt(kSpot)), "a taken item is consumed");
    check(!ledger.consumed(herbAt({kSpot[0] + rule::kSameItemDistance * 2, kSpot[1], kSpot[2]})), "an item far away is not");
    check(ledger.consumedKeys().size() == 2, "the journal lists the consumed items");
    ledger.put(herbAt(kSpot));
    check(!ledger.consumed(herbAt(kSpot)) && ledger.consumedKeys().size() == 1, "a put at the spot makes it takeable again");
    rule::Ledger joiner;
    for (const rule::Key& key : ledger.consumedKeys()) joiner.taken(key);
    check(joiner.consumed({kScene, kAmmo, {500.0f, 0.0f, 0.0f}}), "a joiner that adopts the journal knows the item is gone");
    ledger.clear();
    check(ledger.consumedKeys().empty(), "a save forgets the journal");
}

void testHoldGrantedAfterAnswer() {
    rule::TakeHold hold;
    check(hold.step(kStart, std::nullopt) == Step::Submit, "an idle hold asks to submit");
    hold.sent(kStart);
    check(hold.step(kStart + 10, std::nullopt) == Step::Wait, "no answer yet: wait");
    check(hold.step(kStart + 20, Answer::Granted) == Step::Run, "granted: run");
    check(hold.step(kStart + 30, std::nullopt) == Step::Run, "the grant holds for the rest of the take phase");
    hold.reset();
    check(hold.step(kStart + 40, std::nullopt) == Step::Submit, "a new pickup submits again");
}

void testHoldRefused() {
    rule::TakeHold hold;
    hold.step(kStart, std::nullopt);
    hold.sent(kStart);
    check(hold.step(kStart + 10, Answer::Taken) == Step::End, "refused: end");
    check(hold.answer() == Answer::Taken, "the reason is kept");
    check(hold.step(kStart + 20, std::nullopt) == Step::End, "the refusal stands");
}

void testHoldStale() {
    rule::TakeHold hold;
    hold.step(kStart, std::nullopt);
    hold.sent(kStart);
    check(hold.step(kStart + 10, Answer::Stale) == Step::End, "a request from before the host's load ends the pickup");
    check(hold.answer() == Answer::Stale, "the reason is kept");
    check(std::string(rule::name(Answer::Stale)) == "stale", "and named in the refusal line");
    check(rule::kLastWireAnswer == static_cast<uint8_t>(Answer::Stale) && Answer::TimedOut > Answer::Stale,
          "stale travels, the local timeout does not");
}

void testHoldTimesOut() {
    rule::TakeHold hold;
    hold.step(kStart, std::nullopt);
    hold.sent(kStart);
    check(hold.step(kStart + rule::kHoldTimeoutMs - 1, std::nullopt) == Step::Wait, "still waiting just before the limit");
    check(hold.step(kStart + rule::kHoldTimeoutMs, std::nullopt) == Step::End, "no answer in time ends the pickup");
    check(hold.answer() == Answer::TimedOut, "the reason is the timeout");
    check(hold.step(kStart + rule::kHoldTimeoutMs + 1, Answer::Granted) == Step::End, "a late grant changes nothing");
}

void testHoldTimeoutAcrossClockWrap() {
    rule::TakeHold hold;
    const uint32_t nearWrap = 0xfffffff0u;
    hold.step(nearWrap, std::nullopt);
    hold.sent(nearWrap);
    check(hold.step(nearWrap + 100, std::nullopt) == Step::Wait, "waiting across the wrap");
    check(hold.step(nearWrap + rule::kHoldTimeoutMs, std::nullopt) == Step::End, "timing out across the wrap");
}

void testHostSettlesAtOnce() {
    rule::TakeHold hold;
    check(hold.step(kStart, std::nullopt) == Step::Submit, "the host submits");
    hold.settle(Answer::Granted);
    check(hold.step(kStart, std::nullopt) == Step::Run, "its own grant runs at once");
    hold.reset();
    hold.step(kStart, std::nullopt);
    hold.settle(Answer::Claimed);
    check(hold.step(kStart, std::nullopt) == Step::End, "its own refusal ends at once");
}

void testTwoRacersEndInOneItem() {
    rule::Ledger ledger;
    rule::TakeHold hostHold;
    rule::TakeHold guestHold;
    hostHold.step(kStart, std::nullopt);
    guestHold.step(kStart, std::nullopt);
    guestHold.sent(kStart);
    const Answer hostAnswer = ledger.request(herbAt(kSpot), kHost, kStart + 40).answer;
    const Answer guestAnswer = ledger.request(herbAt(kSpot), kGuest, kStart + 60).answer;
    hostHold.settle(hostAnswer);
    const Step hostStep = hostHold.step(kStart + 60, std::nullopt);
    const Step guestStep = guestHold.step(kStart + 100, guestAnswer);
    check(hostStep == Step::Run && guestStep == Step::End, "the host's earlier take is the only one that runs");
}

}  // namespace

int main() {
    testFirstRequestWinsInBothOrders();
    testSerialsCount();
    testDistinctItems();
    testHolderAsksAgain();
    testReservationExpires();
    testTakenIsFinal();
    testTakenWithoutGrant();
    testPutForgets();
    testClearOnSave();
    testBounded();
    testConsumedJournal();
    testHoldGrantedAfterAnswer();
    testHoldRefused();
    testHoldStale();
    testHoldTimesOut();
    testHoldTimeoutAcrossClockWrap();
    testHostSettlesAtOnce();
    testTwoRacersEndInOneItem();
    if (g_failures == 0) std::printf("item_ledger_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
