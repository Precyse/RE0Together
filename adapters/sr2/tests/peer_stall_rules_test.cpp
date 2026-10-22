// The stalled peer rules (no game): hide after 3 s of silence, show when it sends again, forget leavers, no rate report of 0.
#include "../src/peer_stall_rules.h"
#include "check.h"

using peer_stall_rules::Change;
using peer_stall_rules::kStallMs;
using peer_stall_rules::Key;
using peer_stall_rules::Watch;

int main() {
    check(peer_stall_rules::silenceOf(5000, 2000) == 3000, "silence is the clock minus the last receive");
    check(peer_stall_rules::silenceOf(10, 0xFFFFFFF0) == 0x19, "a wrapped clock loses one more, as 0x7FA9E0 counts it");

    const Key p3{0x61055CBC, 2};
    Watch watch;
    check(watch.update(p3, kStallMs - 1).change == Change::None, "a member silent just under 3 s is not stalled");
    const auto hidden = watch.update(p3, kStallMs);
    check(hidden.change == Change::Hide && watch.held(p3), "3 s of silence hides its human");
    check(watch.update(p3, 30000).change == Change::None, "a stall is hidden once");
    const auto shown = watch.update(p3, 15);
    check(shown.change == Change::Show && shown.longestMs == 30000 && !watch.held(p3),
          "the first datagram after 30 s shows it again, with the length of the stall");

    const Key p2{0x6105170C, 1};
    watch.update(p3, kStallMs);
    watch.update(p2, kStallMs);
    watch.keepOnly([&](Key key) { return key == p2; });
    check(!watch.held(p3) && watch.held(p2) && watch.size() == 1, "a member that left is forgotten, the others stay held");
    check(watch.update({0x61055CBC, 3}, 0).change == Change::None, "a rejoiner in another slot starts fresh");

    check(!peer_stall_rules::reportsRate(0), "no receive sample in 10 s (just resumed) is no rate report");
    check(peer_stall_rules::reportsRate(0x2000), "a measured rate is reported");
    return g_failures == 0 ? 0 : 1;
}
