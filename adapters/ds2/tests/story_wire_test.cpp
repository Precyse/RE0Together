// story_wire: the STORY_EVENT payload checks (no game).
#include <cstdio>
#include <cstring>
#include <vector>

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

    std::vector<uint8_t> shorter = bytes(mission);
    shorter.pop_back();
    check(!decode(shorter, got), "a short payload is rejected");
    std::vector<uint8_t> longer = bytes(mission);
    longer.push_back(0);
    check(!decode(longer, got), "a long payload is rejected");
    Event unknown = mission;
    unknown.kind = 9;
    check(!decode(bytes(unknown), got), "an unknown kind is rejected");
    unknown.kind = 0;
    check(!decode(bytes(unknown), got), "kind zero is rejected");

    std::printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
    return g_failures ? 1 : 0;
}
