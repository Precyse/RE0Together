#include "item_ledger.h"

#include <mutex>
#include <optional>

#include "character_owner.h"
#include "debug_stats.h"
#include "frame_router.h"
#include "item_wire.h"
#include "log.h"
#include "settled_copy.h"
#include "state_net.h"

namespace {

namespace rule = item_ledger_rule;
namespace wire = item_wire;
using debug_stats::Counter;
using item_ledger::Key;
using item_ledger::Resolution;
using item_ledger::Take;
using item_ledger::Verdict;

struct Received {
    uint16_t seq;
    rule::Answer answer;
};

std::mutex g_mutex;
rule::Ledger g_ledger;               // guarded by g_mutex: the host's, touched by the net thread and the game thread
std::optional<Received> g_received;  // guarded by g_mutex: a guest's answer, net thread to game thread
rule::TakeHold g_hold;               // game thread
uint16_t g_requestSeq = 0;           // game thread

uint32_t nowMs() { return static_cast<uint32_t>(monotonicMs()); }

// Host: decides one request and logs it.
rule::Decision decide(const Key& key, uint8_t slot) {
    rule::Decision decision;
    {
        std::lock_guard lock(g_mutex);
        decision = g_ledger.request(key, slot, nowMs());
    }
    if (decision.answer == rule::Answer::Granted) {
        logger::write("item_ledger: take scene 0x%04x item 0x%02x committed to slot %d serial %u", key.scene, key.itemId,
                      slot, decision.serial);
        debug_stats::count(Counter::TakesGranted);
    } else {
        logger::write("item_ledger: take scene 0x%04x item 0x%02x refused (%s) for slot %d", key.scene, key.itemId,
                      rule::name(decision.answer), slot);
        debug_stats::count(Counter::TakesRefused);
    }
    return decision;
}

void onRequest(const GameFrame& frame) {
    const auto request = wire::decodeRequest(frame.payload);
    if (!request) {
        logger::writeUnlessRepeated("item_ledger: refused a malformed take request from slot %d", frame.slot);
        return;
    }
    const rule::Decision decision = decide(wire::keyOf(*request), frame.slot);
    const wire::TakeResult result{decision.serial, request->seq, request->scene, frame.slot,
                                  static_cast<uint8_t>(decision.answer), 0};
    state_net::send(wire::kMsgTakeResult, true, proto::bytesOf(result));
}

void onResult(const GameFrame& frame) {
    const auto result = wire::decodeResult(frame.payload);
    if (!result) {
        logger::writeUnlessRepeated("item_ledger: refused a malformed take result from slot %d", frame.slot);
        return;
    }
    if (result->requester != character_owner::localSlot()) return;
    std::lock_guard lock(g_mutex);
    g_received = Received{result->seq, static_cast<rule::Answer>(result->answer)};
}

// The answer to the request in flight, when it has come.
std::optional<rule::Answer> answerInFlight() {
    std::lock_guard lock(g_mutex);
    if (!g_received || g_received->seq != g_requestSeq) return std::nullopt;
    return g_received->answer;
}

bool journalSaysTaken(const Key& key) {
    std::lock_guard lock(g_mutex);
    return g_ledger.consumed(key);
}

// Starts the pickup's request. True when one went to the host.
bool submit(const Take& take) {
    const Key key{take.scene, take.itemId, take.pos};
    if (character_owner::isHost()) {
        g_hold.settle(decide(key, character_owner::localSlot()).answer);
        return false;
    }
    if (journalSaysTaken(key)) {
        logger::write("item_ledger: take scene 0x%04x item 0x%02x refused (taken) from the journal", key.scene, key.itemId);
        debug_stats::count(Counter::TakesRefused);
        g_hold.settle(rule::Answer::Taken);
        return false;
    }
    ++g_requestSeq;
    const wire::TakeRequest request{g_requestSeq, take.scene, take.itemId, take.count, take.pos};
    if (!state_net::send(wire::kMsgTakeRequest, true, proto::bytesOf(request))) {
        g_hold.settle(rule::Answer::Granted);  // not linked: nobody to arbitrate
        return false;
    }
    g_hold.sent(nowMs());
    debug_stats::count(Counter::PickupsHeld);
    return true;
}

}  // namespace

namespace item_ledger {

bool enable() {
    return frame_router::add(wire::kMsgTakeRequest, frame_router::SenderPolicy::GuestOnly, onRequest) &&
           frame_router::add(wire::kMsgTakeResult, frame_router::SenderPolicy::HostOnly, onResult);
}

Resolution resolveTake(const Take& take) {
    Resolution resolution{Verdict::Run, false, rule::Answer::Granted};
    rule::TakeHold::Step step = g_hold.step(nowMs(), std::nullopt);
    if (step == rule::TakeHold::Step::Submit) {
        resolution.requested = submit(take);
        step = g_hold.step(nowMs(), std::nullopt);
    } else if (step == rule::TakeHold::Step::Wait) {
        step = g_hold.step(nowMs(), answerInFlight());
    }
    switch (step) {
        case rule::TakeHold::Step::Run: break;
        case rule::TakeHold::Step::End:
            resolution.verdict = Verdict::End;
            resolution.answer = g_hold.answer();
            break;
        case rule::TakeHold::Step::Submit:
        case rule::TakeHold::Step::Wait: resolution.verdict = Verdict::Hold; break;
    }
    return resolution;
}

void resetTake() { g_hold.reset(); }

void notePut(const Key& key) {
    std::lock_guard lock(g_mutex);
    g_ledger.put(key);
}

void noteTaken(const Key& key) {
    std::lock_guard lock(g_mutex);
    g_ledger.taken(key);
}

std::vector<Key> consumed() {
    std::lock_guard lock(g_mutex);
    return g_ledger.consumedKeys();
}

void adoptConsumed(const std::vector<Key>& keys) {
    std::lock_guard lock(g_mutex);
    for (const Key& key : keys) g_ledger.taken(key);
}

void clear() {
    std::lock_guard lock(g_mutex);
    g_ledger.clear();
}

}  // namespace item_ledger
