// The creation queue overflow of milestone 9 (no game): handles that do not fit wait in order and move in after the drain.
#include <cstdio>

#include "../src/creation_queue_rules.h"
#include "check.h"

namespace {

using creation_queue_rules::Overflow;

constexpr uintptr_t kMemberA = 0x1000;
constexpr uintptr_t kMemberB = 0x2000;

}  // namespace

int main() {
    using namespace creation_queue_rules;

    check(hasRoom(0) && hasRoom(kCapacity - 1) && !hasRoom(kCapacity), "a queue of 0x100 has room until it holds 0x100");
    check(maskAfterQueue(0xFFFF, 2) == 0xFFFB, "queuing for slot 2 clears its bit of the created mask");

    Overflow overflow;
    uint32_t out[kCapacity];
    check(overflow.take(kMemberA, out, kCapacity) == 0 && overflow.pending(kMemberA) == 0, "nothing waits for a member that never overflowed");
    for (uint32_t handle = 1; handle <= kCapacity + 5; ++handle) overflow.add(kMemberA, handle);
    overflow.add(kMemberB, 99);
    check(overflow.pending(kMemberA) == kCapacity + 5 && overflow.pending(kMemberB) == 1, "each member has its own list");
    check(overflow.take(kMemberA, out, kCapacity) == kCapacity && out[0] == 1 && out[kCapacity - 1] == kCapacity,
          "a full queue's worth moves in, the oldest first");
    check(overflow.pending(kMemberA) == 5 && overflow.take(kMemberA, out, kCapacity) == 5 && out[0] == kCapacity + 1 && out[4] == kCapacity + 5,
          "the rest follows on the next drain");
    check(overflow.pending(kMemberA) == 0 && overflow.take(kMemberB, out, 0) == 0 && overflow.pending(kMemberB) == 1,
          "an empty list is gone; no room moves nothing");

    for (uint32_t handle = 0; handle < kMaxOverflow + 3; ++handle) overflow.add(kMemberA, handle);
    check(overflow.pending(kMemberA) == kMaxOverflow, "the list is bounded");
    uint32_t first[1];
    overflow.take(kMemberA, first, 1);
    check(first[0] == 3, "past the bound the oldest handles are the ones dropped");
    return g_failures == 0 ? 0 : 1;
}
