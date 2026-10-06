// cutscene_wire and cutscene_gate: the payload checks and the host's wait for its guests (no game).
#include <cstdio>
#include <cstring>
#include <vector>

#include "../src/cutscene_gate.h"
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
    bad.category = 0;
    check(!decode(bytes(bad), got), "category none is not shared");
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

}  // namespace

int main() {
    wireTests();
    gateTests();
    std::printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
    return g_failures ? 1 : 0;
}
