// Checks the host-ordered story flag rules and their wire format (no game). Exit 0 when every check passes.
#include <cstdio>
#include <cstring>

#include "../src/flag_sequencer_rule.h"
#include "../src/flag_sequencer_wire.h"

namespace {

namespace rule = flag_sequencer_rule;
namespace wire = flag_sequencer_wire;
using flag_diff::Change;
using flag_diff::Words;

constexpr uint8_t kHost = 0;
constexpr uint8_t kGuest = 1;
constexpr uint16_t kWord = 3;
constexpr uint32_t kBit = 1u << 5;
constexpr uint32_t kOtherBit = 1u << 9;
int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

Change setBits(uint16_t word, uint32_t mask) { return {word, 0, mask, 0}; }
Change clearBits(uint16_t word, uint32_t mask) { return {word, 0, 0, mask}; }

void testEffective() {
    Words words{};
    words[kWord] = kBit;
    const auto changes = rule::effective(words, {{kWord, 0, kBit | kOtherBit, 0}, clearBits(kWord, kBit), setBits(7, 1)});
    check(changes.size() == 3, "a clear of a set bit and a set of a clear bit both count");
    check(changes[0].set == kOtherBit && changes[0].clear == 0, "a bit already set is dropped from the set mask");
    check(changes[1].clear == kBit, "a set bit is cleared");
    check(rule::effective(words, {setBits(kWord, kBit)}).empty(), "a proposal that changes nothing has no effect");
    const Change outside = rule::effectiveBits(words, setBits(static_cast<uint16_t>(flag_diff::kWords), 1));
    check(outside.set == 0 && outside.clear == 0, "a word outside the bitset has no effective bits");
    check(rule::bitCount(0xf0f0u) == 8 && rule::bitCount(0) == 0, "bit count");
}

void testWire() {
    const std::vector<Change> changes = {setBits(2, 0x11), clearBits(70, 0x80000000u)};
    const auto proposal = wire::decodePropose(wire::encodePropose(changes));
    check(proposal && proposal->size() == 2 && (*proposal)[1].word == 70, "a proposal round trips");

    const wire::Commit commit{42, kGuest, changes};
    const auto decoded = wire::decodeCommit(wire::encodeCommit(commit));
    check(decoded && decoded->serial == 42 && decoded->origin == kGuest && decoded->changes.size() == 2, "a commit round trips");
    check(wire::decodeCommit(wire::encodeCommit({7, kHost, {}})).has_value(), "an empty commit is valid");

    std::vector<uint8_t> bytes = wire::encodeCommit(commit);
    bytes.pop_back();
    check(!wire::decodeCommit(bytes), "a short commit is refused");
    bytes = wire::encodeCommit(commit);
    bytes.push_back(0);
    check(!wire::decodeCommit(bytes), "a long commit is refused");
    check(!wire::decodeCommit(std::vector<uint8_t>(3)), "a commit without a header is refused");
    check(!wire::decodePropose(std::vector<uint8_t>(2)), "a proposal without a header is refused");

    check(!wire::decodePropose(wire::encodePropose({setBits(static_cast<uint16_t>(flag_diff::kWords), 1)})),
          "a word outside the bitset is refused");
    check(!wire::decodePropose(wire::encodePropose({{kWord, 0, kBit, kBit}})), "a bit both set and cleared is refused");

    std::vector<Change> tooMany(flag_diff::kWords + 1, setBits(0, 1));
    check(!wire::decodePropose(wire::encodePropose(tooMany)), "more changes than words is refused");
    check(sizeof(wire::CommitHeader) == 8 && sizeof(wire::ProposeHeader) == 4, "header sizes");
}

void testHostOrder() {
    rule::HostOrder order;
    const wire::Commit first = order.issue(kHost, {setBits(0, 1)});
    const wire::Commit second = order.issue(kGuest, {});
    check(first.serial == 1 && second.serial == 2, "commits are numbered 1, 2, ...");
    check(second.origin == kGuest && second.changes.empty(), "the origin and an empty answer are kept");
}

void testSerialTracker() {
    rule::SerialTracker tracker;
    check(tracker.accept(10) == rule::Verdict::InOrder, "the first commit sets the baseline");
    check(tracker.accept(11) == rule::Verdict::InOrder, "the next serial is in order");
    check(tracker.accept(11) == rule::Verdict::Duplicate, "a repeated serial is a duplicate");
    check(tracker.accept(9) == rule::Verdict::Duplicate, "an older serial is a duplicate");
    check(tracker.accept(14) == rule::Verdict::Gap, "skipped serials are a gap");
    check(tracker.expected() == 15, "a gap restarts the baseline at the commit that revealed it");
    check(tracker.accept(15) == rule::Verdict::InOrder, "numbering carries on after a gap");
    tracker.reset();
    check(tracker.accept(100) == rule::Verdict::InOrder, "a reset takes the next commit as the baseline");

    rule::SerialTracker wrapping;
    wrapping.accept(0xffffffffu);
    check(wrapping.accept(0) == rule::Verdict::InOrder, "serials wrap");
}

void testLedger() {
    rule::ProposalLedger ledger;
    ledger.proposed({setBits(kWord, kBit)});
    ledger.proposed({clearBits(kWord, kOtherBit), setBits(9, 1)});
    check(ledger.touched(kWord) == (kBit | kOtherBit), "bits of every unanswered proposal");
    check(ledger.touched(9) == 1 && ledger.touched(10) == 0, "per word");
    ledger.answered();
    check(ledger.touched(kWord) == kOtherBit, "an answer retires the oldest proposal");
    ledger.reset();
    ledger.answered();
    check(ledger.touched(kWord) == 0, "answering nothing is harmless");

    for (size_t i = 0; i <= rule::kMaxUnanswered; ++i) ledger.proposed({setBits(static_cast<uint16_t>(i % 8), 1)});
    for (size_t i = 0; i < rule::kMaxUnanswered; ++i) ledger.answered();
    check(ledger.touched(0) == 0 && ledger.touched(1) == 0, "the ledger is bounded");
}

// The host clears a bit in the same window in which the guest sets it: the host's order (its clear, then the guest's
// proposal) is the end state on both machines.
void testRaceEndsAtTheHostsOrder() {
    Words hostWords{};
    Words guestWords{};
    hostWords[kWord] = kBit;                // the host clears it natively
    guestWords[kWord] = kOtherBit;          // the guest sets the same bit natively (and another one)
    hostWords[kWord] &= ~kBit;
    guestWords[kWord] |= kBit;
    rule::HostOrder hostOrder;
    rule::GuestOrder guestOrder;
    const std::vector<Change> proposal = {setBits(kWord, kBit | kOtherBit)};
    guestOrder.proposed(proposal);

    std::vector<wire::Commit> commits = {hostOrder.issue(kHost, {clearBits(kWord, kBit)})};
    Words hostModel = hostWords;
    const auto effective = rule::effective(hostModel, proposal);
    for (const Change& change : effective) flag_diff::apply(hostModel, change);
    commits.push_back(hostOrder.issue(kGuest, effective));
    check(commits[1].serial == 2, "the proposal is numbered after the host's own change");

    size_t overrides = 0;
    for (const wire::Commit& commit : commits) {
        const rule::Receipt receipt = guestOrder.receive(commit, kGuest, guestWords);
        check(receipt.apply && !receipt.gap, "every commit applies in order");
        overrides += receipt.overrode.size();
    }
    check(overrides == 1, "only the host's clear overrode a bit the guest had proposed");
    check(guestWords == hostModel, "both machines end equal");
    check(hostModel[kWord] == (kBit | kOtherBit), "the host's order wins: the guest's set came last");
}

void testHostClearAfterGuestSet() {
    Words guestWords{};
    guestWords[kWord] = kBit;
    rule::GuestOrder guest;
    guest.proposed({setBits(kWord, kBit)});

    const rule::Receipt first = guest.receive({1, kHost, {clearBits(kWord, kBit)}}, kGuest, guestWords);
    check(first.overrode.size() == 1 && first.overrode[0].clear == kBit, "a host clear overrides the guest's pending set");
    check((guestWords[kWord] & kBit) == 0, "the committed value is the model's");
    const rule::Receipt answer = guest.receive({2, kGuest, {}}, kGuest, guestWords);
    check(answer.apply && answer.overrode.empty(), "an empty answer retires the proposal");
}

void testUntouchedBitIsPlainApply() {
    Words guestWords{};
    rule::GuestOrder guest;
    const rule::Receipt receipt = guest.receive({5, kHost, {setBits(kWord, kBit)}}, kGuest, guestWords);
    check(receipt.apply && receipt.overrode.empty(), "a bit the guest never touched is applied, not corrected");
    check(guestWords[kWord] == kBit, "and lands in the model");
}

void testEchoNeedsNoCorrection() {
    Words guestWords{};
    guestWords[kWord] = kBit;
    rule::GuestOrder guest;
    guest.proposed({setBits(kWord, kBit)});
    const rule::Receipt receipt = guest.receive({1, kGuest, {setBits(kWord, kBit)}}, kGuest, guestWords);
    check(receipt.apply && receipt.overrode.empty(), "the echo of the guest's own proposal agrees");
}

void testGapAndDuplicate() {
    Words guestWords{};
    rule::GuestOrder guest;
    guest.receive({4, kHost, {}}, kGuest, guestWords);
    const rule::Receipt gap = guest.receive({7, kHost, {setBits(kWord, kBit)}}, kGuest, guestWords);
    check(gap.apply && gap.gap && gap.expected == 5, "a serial gap is reported with the expected serial");
    check(guestWords[kWord] == kBit, "the commit that revealed the gap still applies");
    const rule::Receipt repeat = guest.receive({7, kHost, {clearBits(kWord, kBit)}}, kGuest, guestWords);
    check(!repeat.apply && guestWords[kWord] == kBit, "a repeated commit is ignored");
    guest.reset();
    check(guest.receive({1000, kHost, {}}, kGuest, guestWords).apply, "after a reset any serial is the baseline");
}

}  // namespace

int main() {
    testEffective();
    testWire();
    testHostOrder();
    testSerialTracker();
    testLedger();
    testRaceEndsAtTheHostsOrder();
    testHostClearAfterGuestSet();
    testUntouchedBitIsPlainApply();
    testEchoNeedsNoCorrection();
    testGapAndDuplicate();
    if (g_failures == 0) std::printf("flag_sequencer_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
