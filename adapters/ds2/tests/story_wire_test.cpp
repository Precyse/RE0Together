// story_wire: the STORY_EVENT payload checks (no game).
#include <cstdio>
#include <cstring>
#include <vector>

#include "../src/story_ledger.h"
#include "../src/story_replay.h"
#include "../src/story_wire.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

std::vector<uint8_t> bytes(const story_wire::Event& event) {
    std::vector<uint8_t> out(sizeof(event));
    std::memcpy(out.data(), &event, sizeof(event));
    return out;
}

}  // namespace

int main() {
    using namespace story_wire;
    Event mission{};
    mission.kind = static_cast<uint8_t>(Kind::MissionStart);
    mission.a = 7;
    mission.b = -1;
    mission.missionId = 0x1000071000018eull;
    Event got{};
    check(decode(bytes(mission), got) && std::memcmp(&got, &mission, sizeof(mission)) == 0, "a mission event round trips");

    Event section{};
    section.kind = static_cast<uint8_t>(Kind::SectionActive);
    for (size_t i = 0; i < kUuidSize; ++i) section.section[i] = static_cast<uint8_t>(i + 1);
    check(decode(bytes(section), got) && isSection(static_cast<Kind>(got.kind)) && got.section[15] == 16,
          "a section event round trips");

    Event area{};
    area.kind = static_cast<uint8_t>(Kind::AreaChange);
    area.a = 300;
    area.b = -1;
    area.flags = 2;
    area.transform[kTransformSize - 1] = 9;
    check(decode(bytes(area), got) && std::memcmp(&got, &area, sizeof(area)) == 0, "an area change round trips");

    Event delivered{};
    delivered.kind = static_cast<uint8_t>(Kind::OrderDelivered);
    delivered.missionId = 0x1000071000018eull;
    check(decode(bytes(delivered), got) && isMission(static_cast<Kind>(got.kind)) && isGuestRequest(static_cast<Kind>(got.kind)) &&
              got.missionId == delivered.missionId,
          "a delivered order round trips and is a guest request");

    std::vector<uint8_t> shorter = bytes(mission);
    shorter.pop_back();
    check(!decode(shorter, got), "a short payload is rejected");
    std::vector<uint8_t> longer = bytes(mission);
    longer.push_back(0);
    check(!decode(longer, got), "a long payload is rejected");
    Event unknown = mission;
    unknown.kind = 10;
    check(!decode(bytes(unknown), got), "an unknown kind is rejected");
    unknown.kind = 0;
    check(!decode(bytes(unknown), got), "kind zero is rejected");

    using namespace story_replay;
    check(applies(Kind::MissionStart, 10) && !applies(Kind::MissionStart, 20) && !applies(Kind::MissionStart, 40),
          "a start replays only for a mission not started here");
    check(applies(Kind::OrderRequest, 0) && !applies(Kind::OrderRequest, 20), "an order request is a start");
    check(applies(Kind::MissionSuccess, 20) && !applies(Kind::MissionSuccess, 10) && !applies(Kind::MissionSuccess, 40),
          "a success replays only for a mission in progress");
    check(applies(Kind::OrderDelivered, 20) && !applies(Kind::OrderDelivered, 40), "a delivered order completes a mission in progress once");
    check(applies(Kind::MissionFail, 20) && !applies(Kind::MissionFail, 30), "a failure replays once");

    story_ledger::Ledger ledger;
    Event start{};
    start.kind = static_cast<uint8_t>(Kind::MissionStart);
    start.missionId = 0xAB;
    start.a = 3;
    start.b = -2;
    start.section[0] = 5;
    ledger.noteStart(start);
    Event replay = ledger.startFor(0xAB);
    check(std::memcmp(&replay, &start, sizeof(start)) == 0, "a mission started by the script replays with its real arguments and section");
    replay = ledger.startFor(0xCD);
    check(replay.kind == static_cast<uint8_t>(Kind::MissionStart) && replay.a == story_ledger::kNoRow && replay.missionId == 0xCD,
          "a mission the host never saw start replays with the no-row fallback");
    ledger.noteEnd(0xAB);
    check(ledger.size() == 0 && ledger.startFor(0xAB).a == story_ledger::kNoRow, "an ended mission leaves the ledger");
    ledger.noteStart(start);
    ledger.clear();
    check(ledger.size() == 0, "a load clears the ledger");

    std::printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
    return g_failures ? 1 : 0;
}
