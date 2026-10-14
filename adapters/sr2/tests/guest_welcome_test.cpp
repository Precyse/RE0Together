// Milestone 5h (no game): when a member is welcomed, the set of welcomed members, and the G1 site's clearance.
#include <cstdio>

#include "../src/guest_welcome_rules.h"
#include "../src/partner_sites.h"
#include "../src/player_cap_patches.h"

namespace {

using guest_welcome_rules::Key;
using guest_welcome_rules::WelcomedSet;

constexpr uintptr_t kSession = 0x20000000;
constexpr uintptr_t kOtherSession = 0x20100000;
constexpr uintptr_t kFirstMember = 0x30000000;
constexpr uintptr_t kSecondMember = 0x30100000;
constexpr uintptr_t kNearby = 0x2E0;  // no milestone 4 site closer to the guest branch (J5 at 0x8CB747 is 0x2ED away)
constexpr uintptr_t kThunk = 0x10006000;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

}  // namespace

int main() {
    using namespace guest_welcome_rules;

    check(!due(0) && !due(0xA) && due(0xB) && due(0xC) && due(0xD), "a member is due from the ready state 0xB on, also when 0xB was skipped");
    check(looksSyncable(0xD) && !looksSyncable(0xB) && !looksSyncable(0xC), "the look sync waits for our own member to be in the game (0xD)");

    WelcomedSet set;
    const Key first{kSession, kFirstMember, 1};
    const Key second{kSession, kSecondMember, 2};
    check(set.add(first), "a member is welcomed the first time");
    check(!set.add(first), "and not again");
    check(set.add(second), "another member is welcomed too");

    set.keepOnly(kSession, {first, second});
    check(set.size() == 2 && !set.add(first), "members still in the session stay welcomed");
    set.keepOnly(kSession, {second});
    check(set.size() == 1 && set.add(first), "a member that left the session is forgotten and welcomed again when it returns");
    set.forget(second);
    check(set.add(second), "a member seen below the ready state is forgotten");
    check(set.add({kSession, kFirstMember, 3}), "the same member object with another slot is another member");
    set.keepOnly(kOtherSession, {first, second});
    check(set.size() == 0, "a new session forgets everybody");

    const player_cap_patches::Thunks thunks{kThunk, kThunk, kThunk};
    bool found = false;
    for (const CodePatch& patch : player_cap_patches::forPlayers(4, thunks)) {
        if (patch.address != 0x8CBA34) continue;
        const uint32_t displacement = static_cast<uint32_t>(kThunk - (0x8CBA34 + 5));
        found = patch.expected == std::vector<uint8_t>{0x83, 0x7F, 0x3C, 0x0D, 0x75, 0x0D} &&
                patch.replacement == std::vector<uint8_t>{0xE9, static_cast<uint8_t>(displacement),
                                                          static_cast<uint8_t>(displacement >> 8),
                                                          static_cast<uint8_t>(displacement >> 16),
                                                          static_cast<uint8_t>(displacement >> 24), 0x90};
    }
    check(found, "G1: 83 7F 3C 0D 75 0D at 0x8CBA34 is checked and becomes jmp rel32 to the thunk and a nop");

    bool clear = true;
    for (const partner_sites::Site& site : partner_sites::kSites) {
        const uintptr_t distance = site.address > 0x8CBA34 ? site.address - 0x8CBA34 : 0x8CBA34 - site.address;
        clear = clear && distance >= kNearby;
    }
    check(clear, "no milestone 4 site within 0x2E0 bytes of the guest branch");
    return g_failures == 0 ? 0 : 1;
}
