// The cutscene handshake rules of milestones 9 and 11 B4 (no game): which message is which handshake, when it is complete, a
// leaver that stops counting, and how long an early ready lives.
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
    sets.started(false);
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
    sets.started(false);
    check(!sets.complete(Handshake::Ready, two, 2), "a cutscene that starts forgets the senders");

    check(isStart(0) && !isStart(1) && !isStart(4), "sub 0 is the start of the host's cutscene");

    // A leaver stops counting at once: the handshakes that were withheld are complete without it.
    SenderSets leave;
    const uintptr_t three[] = {kHost, kGuestTwo, kGuestThree};
    leave.add(Handshake::Ready, kHost);
    leave.add(Handshake::Ready, kGuestTwo);
    leave.setWithheld(Handshake::Ready, true);
    SenderSets::Completed none = leave.memberLeft(kHost, three, 3);
    check(!none.ready && !none.skip && !none.done && leave.withheld(Handshake::Ready), "a sender leaves: the missing remote is still missing");
    leave.add(Handshake::Ready, kHost);
    const SenderSets::Completed last = leave.memberLeft(kGuestThree, three, 3);
    check(last.ready && !last.skip && !last.done && !leave.withheld(Handshake::Ready), "the last missing remote leaves: ready is complete");
    check(!leave.memberLeft(kGuestThree, three, 3).ready, "and is completed once");

    SenderSets other;
    other.add(Handshake::Skip, kHost);
    other.setWithheld(Handshake::Skip, true);
    other.add(Handshake::Done, kHost);
    other.add(Handshake::Done, kGuestTwo);
    other.setWithheld(Handshake::Done, true);
    const SenderSets::Completed both = other.memberLeft(kGuestThree, three, 3);
    check(!both.ready && !both.skip && both.done, "each handshake is judged on its own set: done completes, skip still waits for guest two");

    SenderSets unwithheld;
    unwithheld.add(Handshake::Ready, kHost);
    check(!unwithheld.memberLeft(kGuestTwo, three, 3).ready, "a handshake the game's byte already has set is not written again");

    SenderSets alone;
    alone.setWithheld(Handshake::Done, true);
    const uintptr_t onlyLeaver[] = {kGuestTwo};
    check(alone.memberLeft(kGuestTwo, onlyLeaver, 1).done, "no remote left at all counts as complete");

    SenderSets oneRemote;
    oneRemote.add(Handshake::Ready, kGuestTwo);
    check(oneRemote.complete(Handshake::Ready, onlyLeaver, 1) && !oneRemote.withheld(Handshake::Ready) &&
              !oneRemote.memberLeft(kGuestTwo, onlyLeaver, 1).ready,
          "one remote (the 2-player case): its message completes it, nothing is withheld, its leave writes nothing");

    // The ready set lives as long as the game's byte.
    SenderSets early;
    early.add(Handshake::Ready, kHost);
    early.add(Handshake::Skip, kHost);
    early.add(Handshake::Done, kHost);
    early.started(true);
    check(early.complete(Handshake::Ready, stayed, 1), "a ready that came before a start inside sub 0 survives it");
    check(!early.complete(Handshake::Skip, stayed, 1) && !early.complete(Handshake::Done, stayed, 1), "skip and done always start empty");
    early.started(false);
    check(!early.complete(Handshake::Ready, stayed, 1), "a cutscene the host starts clears the ready set");
    early.add(Handshake::Ready, kHost);
    early.setWithheld(Handshake::Ready, true);
    early.clearReady();
    check(!early.complete(Handshake::Ready, stayed, 1) && !early.withheld(Handshake::Ready) && !early.memberLeft(kHost, stayed, 1).ready,
          "the playback start or the wait giving up (CS1, CS2) clears the ready set, so a stale set cannot carry into the next cutscene");
    return g_failures == 0 ? 0 : 1;
}
