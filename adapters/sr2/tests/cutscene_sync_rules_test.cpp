// The cutscene handshake rules of milestone 9 (no game): which message is which handshake, and when it is complete.
#include <cstdio>

#include "../src/cutscene_sync_rules.h"

namespace {

using namespace cutscene_sync_rules;

constexpr uintptr_t kHost = 0x1000;
constexpr uintptr_t kGuestTwo = 0x2000;
constexpr uintptr_t kGuestThree = 0x3000;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

}  // namespace

int main() {
    check(handshakeOf(0, false) == Handshake::None && handshakeOf(4, false) == Handshake::None && handshakeOf(5, false) == Handshake::None,
          "start, end and the end position pass through");
    check(handshakeOf(1, false) == Handshake::Ready && handshakeOf(3, false) == Handshake::Done, "ready and done are tracked");
    check(handshakeOf(2, false) == Handshake::Skip && handshakeOf(2, true) == Handshake::None,
          "a skip is tracked, unless one press skips the cutscene for everyone");

    SenderSets sets;
    const uintptr_t one[] = {kGuestTwo};
    check(!sets.complete(Handshake::Ready, one, 1), "nobody has sent ready");
    sets.add(Handshake::Ready, kGuestTwo);
    check(sets.complete(Handshake::Ready, one, 1), "one remote (the 2-player case): its message completes it");

    const uintptr_t two[] = {kHost, kGuestThree};
    sets.clear();
    sets.add(Handshake::Ready, kHost);
    check(!sets.complete(Handshake::Ready, two, 2), "two remotes: the first message does not");
    sets.add(Handshake::Ready, kHost);
    check(!sets.complete(Handshake::Ready, two, 2), "the same sender twice does not");
    check(!sets.complete(Handshake::Skip, two, 2) && !sets.complete(Handshake::Done, two, 2), "each handshake has its own set");
    sets.add(Handshake::Ready, kGuestThree);
    check(sets.complete(Handshake::Ready, two, 2), "the last remote's message completes it");

    const uintptr_t stayed[] = {kHost};
    check(sets.complete(Handshake::Ready, stayed, 1), "a remote that left stops counting");
    check(sets.complete(Handshake::Ready, nullptr, 0), "nobody required: complete");
    sets.clear();
    check(!sets.complete(Handshake::Ready, two, 2), "a cutscene that starts forgets the senders");
    return g_failures == 0 ? 0 : 1;
}
