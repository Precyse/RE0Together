// Milestone 5i, sections 9 and 10 (no game): the partial group rule, the lock rules, the log rate limit, and the four spawn
// group entries as data.
#include <algorithm>
#include <cstdio>
#include <cstring>

#include "../src/partner_sites.h"
#include "../src/player_cap_patches.h"
#include "../src/rate_limit.h"
#include "../src/spawner_rules.h"
#include "juiced_targets.h"

namespace {

constexpr uintptr_t kThunk = 0x10002000;
constexpr uintptr_t kJuicedMargin = 16;
constexpr uintptr_t kNearbyMargin = 0x500;  // no milestone 4 site closer to the entries (K2 at 0x9129FB is 0x5D5 from the crew free)
constexpr uint32_t kFourPlayers = 4;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

const CodePatch* patchAt(const std::vector<CodePatch>& patches, uintptr_t address) {
    for (const CodePatch& patch : patches) {
        if (patch.address == address) return &patch;
    }
    return nullptr;
}

}  // namespace

int main() {
    using spawner_rules::Group;

    check(!spawner_rules::isPartial({0x1000, {0x10, 0x20, 0x30}}), "a group whose slots are all filled is complete");
    check(spawner_rules::isPartial({0x1000, {0x10, 0, 0x30}}) && spawner_rules::isPartial({0x1000, {0}}),
          "a group with a null slot is partial, also the first slot");
    check(!spawner_rules::isPartial({0x1000, {}}), "a template without slots has nothing missing");
    const std::vector<Group> groups = {{0x1000, {1, 2}}, {0x2000, {1, 0}}, {0x3000, {0, 0}}, {0x4000, {5}}};
    check(spawner_rules::partialGroups(groups) == std::vector<uintptr_t>{0x2000, 0x3000}, "the trace names the partial groups, in order");
    check(spawner_rules::partialGroups({}).empty(), "an empty list has none");
    check(spawner_rules::outermost(1) && !spawner_rules::outermost(2) && !spawner_rules::outermost(3),
          "the list is looked at only by a call that took the lock just now, not one inside the tick that held it");
    check(spawner_rules::isLongWait(20001) && !spawner_rules::isLongWait(20000) && !spawner_rules::isLongWait(0),
          "a wait over 20 ms is worth a line");
    check(std::strcmp(spawner_rules::siteName(spawner_rules::Site::ChunkUnload), "chunk unload") == 0 &&
              spawner_rules::kSiteCount == 4,
          "four entries, each with a name for the trace");

    rate_limit::RateLimit limit;
    uint32_t triggers = 0;
    check(limit.allow(5000, triggers) && triggers == 1, "the first trigger is logged, standing for one");
    check(!limit.allow(5100, triggers) && !limit.allow(5999, triggers), "triggers within a second are only counted");
    check(limit.allow(6000, triggers) && triggers == 3, "the next line comes a second later and says how many it stands for");
    check(!limit.allow(6500, triggers) && limit.allow(7000, triggers) && triggers == 2, "and so on, once per second");

    const player_cap_patches::Thunks thunks{kThunk, kThunk, kThunk, kThunk, kThunk, kThunk, kThunk};
    const auto patches = player_cap_patches::forPlayers(kFourPlayers, thunks);
    const CodePatch* create = patchAt(patches, 0x910370);
    const CodePatch* parse = patchAt(patches, 0x9140C0);
    const CodePatch* unload = patchAt(patches, 0x914900);
    const CodePatch* crew = patchAt(patches, 0x912FD0);
    check(create && create->expected == std::vector<uint8_t>{0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8} && create->replacement.empty(),
          "S1: 55 8B EC 83 E4 F8 at 0x910370 is checked, the hook writes it");
    check(parse && parse->expected == std::vector<uint8_t>{0x81, 0xEC, 0x4C, 0x02, 0x00, 0x00} && parse->replacement.empty(),
          "S2: 81 EC 4C 02 00 00 at 0x9140C0 is checked");
    check(unload && unload->expected == std::vector<uint8_t>{0xA0, 0x48, 0x8B, 0x52, 0x02} && unload->replacement.empty(),
          "S3: A0 48 8B 52 02 at 0x914900 is checked (a 5 byte instruction, enough for the hook)");
    check(crew && crew->expected == std::vector<uint8_t>{0x51, 0x53, 0x55, 0x8B, 0x6C, 0x24, 0x10} && crew->replacement.empty(),
          "S4: 51 53 55 8B 6C 24 10 at 0x912FD0 is checked");
    check(std::none_of(patches.begin(), patches.end(), [](const CodePatch& p) { return p.address == 0x90D44B; }),
          "C1 (0x90D44B) is gone");
    check(player_cap_patches::forPlayers(2, thunks).empty(), "max_players 2 patches nothing");

    bool clearOfJuiced = true;
    bool clearOfPartner = true;
    for (const CodePatch* patch : {create, parse, unload, crew}) {
        const uintptr_t low = patch->address - kJuicedMargin;
        const uintptr_t high = patch->address + patch->expected.size() + kJuicedMargin;
        clearOfJuiced = clearOfJuiced && std::none_of(std::begin(juiced::kTargets), std::end(juiced::kTargets),
                                                      [&](uint32_t target) { return target >= low && target < high; });
        for (const partner_sites::Site& site : partner_sites::kSites) {
            const uintptr_t distance = site.address > patch->address ? site.address - patch->address : patch->address - site.address;
            clearOfPartner = clearOfPartner && distance >= kNearbyMargin;
        }
    }
    check(clearOfJuiced, "no entry within 16 bytes of an address Juiced touches");
    check(clearOfPartner, "no milestone 4 site within 0x500 bytes of an entry");
    return g_failures == 0 ? 0 : 1;
}
