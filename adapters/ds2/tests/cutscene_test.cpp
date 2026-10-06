// cutscene_wire and cutscene_gate: the payload checks and the host's wait for its guests (no game).
#include <cstdio>
#include <cstring>
#include <vector>

#include "../src/cutscene_gate.h"
#include "../src/cutscene_table.h"
#include "../src/cutscene_wire.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

template <class T>
std::vector<uint8_t> bytes(const T& value) {
    std::vector<uint8_t> out(sizeof(value));
    std::memcpy(out.data(), &value, sizeof(value));
    return out;
}

void wireTests() {
    using namespace cutscene_wire;
    Start start{};
    start.id = 3;
    start.category = kCategoryStory;
    start.stopFrame = 720;
    start.resource[15] = 7;
    start.network[0] = 9;
    Start got{};
    check(decode(bytes(start), got) && std::memcmp(&got, &start, sizeof(start)) == 0, "a START round trips");

    Start bad = start;
    bad.id = 0;
    check(!decode(bytes(bad), got), "START with id zero is rejected");
    bad = start;
    bad.category = 4;
    check(!decode(bytes(bad), got), "the menu radio is not a shared category");
    bad.category = kCategoryLast + 1;
    check(!decode(bytes(bad), got), "a category past the last is rejected");
    bad.category = 0;
    check(decode(bytes(bad), got), "category none travels (a Cutscene game state Sequence)");
    check(isShared(0, kGameStateCutscene) && !isShared(0, 0) && isShared(kCategoryStory, 0) && !isShared(kCategoryMenuRadio, kGameStateCutscene),
          "sharing: cutscene state or a story category, never the menu radio");
    std::vector<uint8_t> shorter = bytes(start);
    shorter.pop_back();
    check(!decode(shorter, got), "a short START is rejected");

    Ready ready{};
    check(!decode(bytes(Ready{0}), ready) && decode(bytes(Ready{5}), ready) && ready.id == 5, "READY needs an id");
    Go go{};
    check(!decode(bytes(Go{0}), go) && decode(bytes(Go{6}), go) && go.id == 6, "GO needs an id");

    End end{};
    end.id = 2;
    end.frame = 300;
    end.reason = kStopScripted;
    End gotEnd{};
    check(decode(bytes(end), gotEnd) && gotEnd.frame == 300, "an END round trips");
    end.reason = kStopReasonLast + 1;
    check(!decode(bytes(end), gotEnd), "an unknown stop reason is rejected");
    end.reason = kStopFinished;
    end.frame = 700;
    check(!endedEarly(end, 720, 24), "a stop near the end frame is the normal end");
    end.frame = 300;
    check(endedEarly(end, 720, 24), "a stop well before the end frame is a skip");
}

void gateTests() {
    using cutscene_gate::ReadyGate;
    ReadyGate gate;
    gate.begin(1, {1, 2}, 1000);
    check(gate.takeOpened(1100).empty(), "the gate stays shut while peers are missing");
    gate.ready(1, 1);
    check(gate.takeOpened(1200).empty(), "one peer ready is not enough");
    gate.ready(1, 2);
    std::vector<cutscene_gate::Opened> opened = gate.takeOpened(1300);
    check(opened.size() == 1 && opened[0].id == 1 && !opened[0].timedOut, "all peers ready opens it");
    check(gate.takeOpened(1400).empty(), "an opened cutscene is returned once");

    gate.begin(2, {1}, 2000);
    opened = gate.takeOpened(2000 + cutscene_gate::kReadyTimeoutMs);
    check(opened.size() == 1 && opened[0].timedOut, "the timeout opens it");

    gate.begin(3, {1, 2}, 3000);
    gate.ready(3, 1);
    gate.drop(2);
    opened = gate.takeOpened(3001);
    check(opened.size() == 1 && !opened[0].timedOut, "a peer that left stops holding it");

    gate.begin(4, {}, 4000);
    check(gate.takeOpened(4000).size() == 1, "no peers opens at once");
    gate.ready(99, 1);
    check(gate.takeOpened(4001).empty(), "an answer for an unknown cutscene is ignored");
}


sequence_info::Info infoFor(uint8_t resourceByte) {
    sequence_info::Info info{};
    info.category = cutscene_wire::kCategoryDollman;
    info.stopFrame = 3257;
    info.resource[0] = resourceByte;
    info.entity[0] = resourceByte;
    return info;
}

void tableTests() {
    using namespace cutscene_table;
    constexpr uintptr_t kSeq = 0x1000;
    const sequence_info::Info info = infoFor(7);

    Table host;
    Verdict v = host.hostDecide(kSeq, info, 1000, 1);
    check(v.hold && v.created && v.id == 1 && host.outStarts.size() == 1, "the host holds a shared start and announces it");
    check(host.hostDecide(kSeq, info, 1016, 1).hold, "the engine's retry stays held");
    host.release(1, 4000, 0);
    v = host.hostDecide(kSeq, info, 4000, 1);
    check(!v.hold && !v.forced, "after the release the next start call passes (no delay)");
    check(!host.hostDecide(kSeq, info, 4016, 1).hold, "and keeps passing while it plays");

    Table delayed;
    delayed.hostDecide(kSeq, info, 1000, 1);
    delayed.release(1, 4000, 30);
    check(delayed.hostDecide(kSeq, info, 4020, 1).hold, "a release delay holds until it has passed");
    check(!delayed.hostDecide(kSeq, info, 4030, 1).hold, "and lets it pass at the delay");

    Table slow;
    slow.hostDecide(kSeq, info, 1000, 1);
    v = slow.hostDecide(kSeq, info, 1000 + kHoldLimitMs + 1, 1);
    check(!v.hold && v.forced, "a hold nobody released ends at the limit");

    Table alone;
    check(!alone.hostDecide(kSeq, info, 1000, 0).hold && alone.outStarts.empty(), "a host without guests holds nothing");

    Table guest;
    cutscene_wire::Start start{};
    start.id = 1;
    start.category = cutscene_wire::kCategoryDollman;
    start.resource[0] = 7;
    guest.arm(start, 1000);
    v = guest.guestDecide(kSeq, info, 1100);
    check(v.hold && guest.outReady.size() == 1 && guest.outReady[0] == 1, "a guest holds its own copy of an announced cutscene and reports ready");
    guest.go(1, 2000);
    check(!guest.guestDecide(kSeq, info, 2000).hold, "the host's go lets it start");
    check(guest.guestDecide(kSeq + 1, infoFor(9), 2100).hold && guest.orphans.size() == 1, "an unannounced shared Sequence stays held");

    Table early;
    early.arm(start, 1000);
    early.go(1, 1500);
    early.guestDecide(kSeq, info, 1600);
    check(!early.guestDecide(kSeq, info, 1616).hold, "a go that arrived before the copy was held releases it at once");

    // Held time counts from the bind, not from the announcement (a slow world load must not force an unsynced start).
    Table late;
    late.arm(start, 1000);
    late.guestDecide(kSeq, info, 1000 + kHoldLimitMs + 3000);
    check(late.guestDecide(kSeq, info, 1000 + kHoldLimitMs + 3500).hold, "the hold limit counts from the bind, not the announcement");
    check(!late.guestDecide(kSeq, info, 1000 + kHoldLimitMs + 3000 + kHoldLimitMs + 1).hold, "and still ends after the limit from the bind");

    // An unannounced shared Sequence is held for a while, then plays and keeps passing.
    Table lone;
    check(lone.guestDecide(kSeq, info, 100).hold && lone.guestDecide(kSeq, info, 100 + kUnannouncedHoldLimitMs).hold, "an unannounced Sequence is held up to the limit");
    Verdict past = lone.guestDecide(kSeq, info, 101 + kUnannouncedHoldLimitMs);
    check(!past.hold && past.forced && !lone.guestDecide(kSeq, info, 200 + kUnannouncedHoldLimitMs).hold, "then it plays, and its retries keep passing");

    Table far;
    far.arm(start, 1000);
    far.giveUp(1);
    check(far.outReady.size() == 1 && far.outReady[0] == 1 && far.playbacks.empty(), "a guest that cannot play it tells the host ready at once and forgets it");
    far.giveUp(1);
    check(far.outReady.size() == 1, "giving up an unknown cutscene says nothing");
    Table bound;
    bound.arm(start, 1000);
    bound.guestDecide(kSeq, info, 1100);
    bound.outReady.clear();
    bound.giveUp(1);
    check(bound.outReady.empty() && bound.playbacks.size() == 1, "a cutscene already held here is not given up");

    // The failure seen live: a guest's announcement with the host's own id 1 left over from an earlier role swallowed the release.
    Table stale;
    stale.arm(start, 1000);
    stale.hostDecide(kSeq, info, 2000, 1);
    stale.release(1, 5000, 0);
    check(stale.hostDecide(kSeq, info, 5000, 1).hold, "ids from two roles in one table collide (why a role change clears the table)");
    Table fresh;
    fresh.hostDecide(kSeq, info, 2000, 1);
    fresh.release(1, 5000, 0);
    check(!fresh.hostDecide(kSeq, info, 5000, 1).hold, "a table cleared at the role change releases");
}

}  // namespace

int main() {
    wireTests();
    gateTests();
    tableTests();
    std::printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
    return g_failures ? 1 : 0;
}
