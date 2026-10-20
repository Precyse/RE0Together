// The pure rules of the anim and asset streaming per remote (milestone 16 W1, no game): who a streaming_sync message goes to and runs
// for, the need row masks and table size, the answer the game would load, the stream request ledger, the set change gates per sender
// and the trigger presets per member.
#include <cstdio>
#include <span>
#include <string_view>
#include <vector>

#include "../src/anim_stream_rules.h"
#include "check.h"

namespace {

using namespace anim_stream_rules;

constexpr uint8_t kRequest = sr2::kStreamingSubRequest;
constexpr uint8_t kAnswer = sr2::kStreamingSubAnswer;
constexpr uint8_t kDrop = sr2::kStreamingSubDrop;
constexpr uint8_t kUnknownSub = 3;
constexpr uint8_t kHostSlot = 0;
constexpr uint8_t kGuestA = 1;
constexpr uint8_t kGuestB = 2;
constexpr uintptr_t kMemberP2 = 0x2C00000;
constexpr uintptr_t kMemberP3 = 0x2C10000;
constexpr uintptr_t kSession = 0x2D00000;
constexpr uintptr_t kNextSession = 0x2D10000;

void theRecipients() {
    check(recipient(kRequest, false) == Recipient::Host && recipient(kRequest, true) == Recipient::Host, "a request goes to the host, with or without a handler sender");
    check(recipient(kDrop, false) == Recipient::Host && recipient(kDrop, true) == Recipient::Host, "a drop goes to the host");
    check(recipient(kAnswer, true) == Recipient::HandlerSender, "an answer goes to the member whose request the handler runs for");
    check(recipient(kAnswer, false) == Recipient::Broadcast, "an answer with no handler sender is broadcast as shipped");
    check(recipient(kUnknownSub, true) == Recipient::Broadcast && recipient(kUnknownSub, false) == Recipient::Broadcast, "any other sub is broadcast");
}

void theGate() {
    check(gate(kRequest, true, false, false) == Verdict::Run && gate(kDrop, true, false, false) == Verdict::Run, "the host runs requests and drops");
    check(gate(kRequest, false, true, false) == Verdict::NotHost && gate(kDrop, false, false, false) == Verdict::NotHost, "a guest never runs a request or a drop");
    check(gate(kAnswer, true, true, true) == Verdict::AnswerOnHost, "the host never runs an answer");
    check(gate(kAnswer, false, false, true) == Verdict::NotFromHost, "a guest runs an answer only from the host");
    check(gate(kAnswer, false, true, false) == Verdict::NoPendingRequest, "an answer for a group with no pending request is a stale one");
    check(gate(kAnswer, false, true, true) == Verdict::Run, "a guest runs the host's answer for a pending request");
    check(gate(kUnknownSub, true, false, false) == Verdict::Run && gate(kUnknownSub, false, false, false) == Verdict::Run, "any other sub runs (the handler ignores it)");
    check(gate(kRequest, true, false, false) == Verdict::Run && gate(kDrop, true, false, false) == Verdict::Run && gate(kAnswer, false, true, true) == Verdict::Run,
          "the shipped 2 player flows all run: the host gets 0 and 2, the guest gets 1 with a pending request");
    check(std::string_view(dropText(Verdict::NotHost)) == "not the host" && std::string_view(dropText(Verdict::AnswerOnHost)) == "answer on the host" &&
              std::string_view(dropText(Verdict::NotFromHost)) == "answer not from the host" &&
              std::string_view(dropText(Verdict::NoPendingRequest)) == "no pending request" && std::string_view(dropText(Verdict::Run)).empty(),
          "the log text of each reason");
}

void theMasks() {
    check(slotBit(0) == 0x01 && slotBit(1) == 0x02 && slotBit(6) == 0x40 && slotBit(7) == 0x80, "slot bits");
    check(slotBit(8) == 0x80 && slotBit(200) == 0x80, "a slot of 8 or more shares bit 7");
    uint8_t mask = maskAfterSet(0, kGuestA);
    mask = maskAfterSet(mask, kGuestB);
    check(mask == 0x06, "two guests needing one group: both bits");
    check(maskAfterClear(mask, kGuestA) == 0x04, "one leaves: the other's bit stays");
    check(maskAfterClear(0x04, kGuestB) == 0, "the last bit cleared: the game frees the row");
    check(maskAfterSet(0x06, kGuestA) == 0x06, "a set of a bit already set changes nothing");
    check(maskAfterSet(0, std::nullopt) == 0x80, "a set with no owner stores the shared bit");
    check(maskAfterClear(0x86, std::nullopt) == 0, "a clear with no owner clears every bit");
    check(maskAfterClear(0x80, kGuestA) == 0x80, "a clear of another slot leaves the shared bit");
    check(maskAfterSet(0x01, uint8_t{9}) == 0x81 && maskAfterClear(0x81, uint8_t{12}) == 0x01, "slots 7 and above share bit 7");
}

void theNeedRows() {
    check(needRows(2) == sr2::kAnimNeedRows, "2 players: the shipped 8 rows");
    check(needRows(3) == 11 && needRows(4) == 14 && needRows(8) == 26, "3 players 11 rows, 4 players 14, 8 players 26");
}

void theRelocation() {
    constexpr uint32_t kNewBase = 0x3A00000;
    constexpr size_t kNewRows = 14;
    constexpr uint32_t kFields[] = {0, 0xCC, 0xCE, 0xCF};
    bool shippedUnchanged = true;
    bool moved = true;
    for (const uint32_t field : kFields) {
        const uint32_t base = static_cast<uint32_t>(sr2::kAnimNeedTable) + field;
        const uint32_t end = static_cast<uint32_t>(sr2::kAnimNeedTableEnd) + field;
        shippedUnchanged = shippedUnchanged && relocatedNeedRef(base, sr2::kAnimNeedTable, sr2::kAnimNeedRows) == base &&
                           relocatedNeedRef(end, sr2::kAnimNeedTable, sr2::kAnimNeedRows) == end;
        moved = moved && relocatedNeedRef(base, kNewBase, kNewRows) == kNewBase + field &&
                relocatedNeedRef(end, kNewBase, kNewRows) == kNewBase + kNewRows * sr2::kAnimNeedRowBytes + field;
    }
    check(shippedUnchanged, "at 8 rows and the shipped base every value is unchanged");
    check(moved, "a base moves with the new base, an end with the new base plus the rows, each with its field offset");
    check(relocatedNeedRef(0x2229C78, kNewBase, kNewRows) == 0x2229C78 && relocatedNeedRef(0x1000, kNewBase, kNewRows) == 0x1000,
          "a value that is neither stays");
}

void theAnswer() {
    const Pending list[] = {{0x2C5, 7}, {0x2C6, 8}, {0x2C5, 9}};
    const PendingLookup first = pendingFor(list, 0x2C5);
    check(first.found && first.id == 7, "the first pending entry of the group, as 0xA83340 scans");
    check(pendingFor(list, 0x2C6).id == 8 && !pendingFor(list, 0x2C7).found && !pendingFor(std::span<const Pending>(), 0x2C5).found, "another group, none, an empty list");
    check(answerWouldLoad(true, false, {true, 7}, 7), "loaded, pending id matches: loads");
    check(answerWouldLoad(false, true, {true, 7}, 7), "being streamed, pending id matches: loads");
    check(!answerWouldLoad(false, false, {true, 7}, 7), "neither loaded nor streamed: declines");
    check(!answerWouldLoad(true, false, {true, 7}, 8), "a pending id that differs: declines");
    check(answerWouldLoad(true, false, {false, 0}, 8), "no pending entry: the id is not compared");
    check(!answerWouldLoad(false, false, {false, 0}, 8), "no pending entry and not known: declines");
}

RequestKey world(uint32_t id, int8_t category = 1, uint32_t key = 0x55) { return {RequestKind::World, id, category, key}; }
RequestKey character(uint32_t hash, int8_t category = 1) { return {RequestKind::Character, hash, category, 0}; }

void theLedger() {
    RequestLedger ledger;
    ledger.reset(kSession);
    ledger.add(kGuestA, world(0x100));
    ledger.add(kGuestB, world(0x100));
    ledger.add(kGuestA, world(0x100));
    check(ledger.held(kGuestA) == 2 && ledger.held(kGuestB) == 1, "two slots on one key count separately");
    ledger.remove(kGuestB, world(0x100));
    check(ledger.held(kGuestB) == 0 && ledger.held(kGuestA) == 2, "a remove from the other slot leaves the first's count");
    ledger.remove(kGuestB, world(0x100));
    check(ledger.held(kGuestB) == 0, "a remove never goes below 0");
    ledger.add(kGuestA, character(0xBEEF));
    ledger.add(kGuestA, world(0x100, 2));
    check(ledger.held(kGuestA) == 4, "a character request and a world request of another category are other keys");
    ledger.remove(kGuestA, world(0x100, 2, 0x56));
    check(ledger.held(kGuestA) == 4, "a remove with another key does not match");
    const std::vector<HeldRequest> taken = ledger.take(kGuestA);
    uint32_t total = 0;
    for (const HeldRequest& request : taken) total += request.count;
    check(taken.size() == 3 && total == 4 && ledger.held(kGuestA) == 0 && ledger.take(kGuestA).empty(), "a leave takes every key with its count, and the slot is then empty");
    check(taken[0].key == world(0x100) && taken[0].count == 2 && taken[1].key == character(0xBEEF), "in the order they were first asked");
    check(ledger.take(40).empty() && !ledger.add(40, world(1)) && ledger.held(40) == 0, "a slot outside the ledger holds nothing");
}

void theLedgerBound() {
    RequestLedger ledger;
    ledger.reset(kSession);
    bool allFit = true;
    for (uint32_t id = 0; id < RequestLedger::kMaxKeysPerSlot; ++id) allFit = allFit && ledger.add(kGuestA, world(id));
    check(allFit && !ledger.overflowed(), "64 keys fit");
    check(!ledger.add(kGuestA, world(1000)) && ledger.overflowed(), "the 65th key is not recorded and the overflow is flagged");
    check(ledger.add(kGuestA, world(3)) && ledger.held(kGuestA) == RequestLedger::kMaxKeysPerSlot + 1, "a key already held still counts when the slot is full");
    ledger.remove(kGuestA, world(0));
    check(ledger.add(kGuestA, world(1000)), "a key freed by its last remove makes room");
    check(ledger.add(kGuestB, world(1000)), "another slot has its own bound");
}

void theLedgerSession() {
    RequestLedger ledger;
    ledger.reset(kSession);
    ledger.add(kGuestA, world(0x100));
    ledger.reset(kSession);
    check(ledger.held(kGuestA) == 1, "the same session keeps its requests");
    for (uint32_t id = 0; id <= RequestLedger::kMaxKeysPerSlot; ++id) ledger.add(kGuestB, world(id));
    ledger.reset(kNextSession);
    check(ledger.held(kGuestA) == 0 && ledger.held(kGuestB) == 0 && !ledger.overflowed(), "a new session starts empty");
}

void theSenderGates() {
    SenderGates gates;
    SenderGates::Bank p2 = gates.load(kGuestA, kMemberP2);
    check(p2[3] == 0, "a fresh bank starts at 0");
    constexpr size_t kIndex = 3;
    constexpr uint32_t kLate = 100;
    constexpr uint32_t kEarly = 90;
    p2[kIndex] = kLate;
    gates.store(kGuestA, p2);
    gates.noteLoad(kIndex, kLate, kGuestA);
    SenderGates::Bank p3 = gates.load(kGuestB, kMemberP3);
    check(p3[kIndex] == 0, "another sender's bank is its own");
    check(gates.sharedWouldSkip(kIndex, kEarly), "P3 at T=90 on index 3: the shared gate of the shipped game (100 from P2) would skip it");
    check(p3[kIndex] <= kEarly, "the per-sender gate lets it load");
    gates.noteLoad(kIndex, kEarly, kGuestB);
    check(gates.sharedTime(kIndex) == kLate && gates.sharedSlot(kIndex) == kGuestA, "a load the shared gate skips does not change the shadow");
    check(!gates.sharedWouldSkip(kIndex, kLate) && !gates.sharedWouldSkip(kIndex, kLate + 1), "a time at or past the gate loads in both");
    gates.noteLoad(kIndex, kLate + 5, kGuestB);
    check(gates.sharedTime(kIndex) == kLate + 5 && gates.sharedSlot(kIndex) == kGuestB, "a load the shared gate lets through writes the shadow");
    check(gates.load(kGuestA, kMemberP2)[kIndex] == kLate, "the sender's bank is kept between messages");
    check(gates.load(kGuestA, kMemberP3)[kIndex] == 0, "a rejoiner in the slot (another member) starts at 0");
    gates.store(kGuestB, p3);
    gates.resetAll();
    check(gates.load(kGuestB, kMemberP3)[kIndex] == 0 && gates.sharedTime(kIndex) == 0 && !gates.sharedWouldSkip(kIndex, 0), "a reset empties every bank and the shadow");
    check(gates.load(40, kMemberP2) == SenderGates::Bank{} && !gates.sharedWouldSkip(99, 0), "a slot or an index outside is harmless");
}

void theTriggerSlots() {
    constexpr uint32_t kPresetP2 = 0x3000;
    constexpr uint32_t kPresetP3 = 0x3100;
    constexpr uint32_t kPresetP4 = 0x3200;
    TriggerSlots slots;
    slots.syncDefault(kGuestA, kPresetP2);
    check(slots.swapIn(kGuestB) == 0, "P3 entering trigger B starts with no preset of its own");
    slots.swapOut(kGuestB, kPresetP3);
    check(slots.swapIn(kGuestA) == kPresetP2 && slots.swapIn(kGuestB) == kPresetP3, "P3's enter does not touch P2's entry");
    check(slots.swapIn(kNoSlot) == 0 && slots.swapIn(40) == 0, "no sender, no preset");

    check(slots.defaultChanged(kGuestA) && slots.lastDefault() == kGuestA && !slots.defaultChanged(kGuestA), "the default partner is noticed once");
    const uint8_t present = slotBit(kGuestB);
    const std::vector<SlotPreset> gone = slots.gone(present);
    check(gone.size() == 1 && gone[0].slot == kGuestA && gone[0].preset == kPresetP2 && slots.swapIn(kGuestA) == 0 && slots.gone(present).empty(),
          "the default leaves: its entry is released once and emptied");
    check(slots.defaultChanged(kGuestB) && slots.swapIn(kGuestB) == kPresetP3, "[0x2527B68] becomes the new default's preset, not the leaver's");

    slots.swapOut(kHostSlot, kPresetP2);
    slots.swapOut(3, kPresetP4);
    const std::vector<SlotPreset> order = slots.gone(slotBit(kHostSlot));
    check(order.size() == 2 && order[0].slot == kGuestB && order[1].slot == 3, "the entries to release come in slot order");

    TriggerSlots cleanup;
    cleanup.swapOut(kHostSlot, kPresetP2);
    cleanup.swapOut(kGuestB, kPresetP3);
    cleanup.swapOut(3, kPresetP4);
    const std::vector<SlotPreset> passes = cleanup.cleanupPasses(kGuestB);
    check(passes.size() == 2 && passes[0].slot == kHostSlot && passes[0].preset == kPresetP2 && passes[1].slot == 3,
          "the cleanup passes are the set entries other than the default's, in slot order");
    check(cleanup.swapIn(kGuestB) == 0 && cleanup.cleanupPasses(kGuestB).empty(), "the cleanup empties the table (the game releases the default's own)");

    TriggerSlots reset;
    reset.swapOut(kGuestA, kPresetP2);
    reset.defaultChanged(kGuestA);
    reset.clear();
    check(reset.swapIn(kGuestA) == 0 && reset.lastDefault() == kNoSlot, "a reset empties the table and forgets the default");
    check(releasableCategory(4) && releasableCategory(6) && !releasableCategory(0) && !releasableCategory(5), "only categories 4 and 6 are released");
}

}  // namespace

int main() {
    theRecipients();
    theGate();
    theMasks();
    theNeedRows();
    theRelocation();
    theAnswer();
    theLedger();
    theLedgerBound();
    theLedgerSession();
    theSenderGates();
    theTriggerSlots();
    return g_failures == 0 ? 0 : 1;
}
