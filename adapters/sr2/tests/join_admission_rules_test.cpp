// The join admission rules of milestone 9 (no game): when a request waits, the wait answer, the queue of joiners.
#include <cstdio>

#include "../src/join_admission_rules.h"

namespace {

using namespace join_admission_rules;

constexpr uintptr_t kFirst = 0x1000;
constexpr uintptr_t kSecond = 0x2000;
constexpr uintptr_t kThird = 0x3000;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

}  // namespace

int main() {
    check(!busy(0, 0, 0) && busy(1, 0, 0) && busy(0, 1, 0) && busy(0, 0, sr2::kFriendsQueryHandler) && !busy(0, 0, 0x1234),
          "an admission is open with its dialog, a deferred answer or the friends query");
    check(!mustWait(0, kSecond, true), "no pending joiner: the request goes through");
    check(!mustWait(kFirst, kFirst, true), "the pending joiner asking again goes through");
    check(!mustWait(kFirst, kSecond, false), "the first joiner is answered: the second goes through");
    check(mustWait(kFirst, kSecond, true), "a second joiner while the first is being admitted waits");

    const Message narrow = waitMessage(false);
    check(narrow.length == 5 && narrow.bytes[0] == 0x19 && narrow.bytes[1] == 2 && narrow.bytes[2] == 0 && narrow.bytes[3] == 0 &&
              narrow.bytes[4] == 2,
          "the wait answer: id 0x19, payload length 2, type 0, code 2");
    const Message wide = waitMessage(true);
    check(wide.length == 6 && wide.bytes[1] == 3 && wide.bytes[4] == 0 && wide.bytes[5] == 2, "with the wide header one more byte");

    WaitingJoiners waiting;
    waiting.add(kSecond, 1);
    waiting.add(kThird, 0);
    waiting.add(kSecond, 0);
    check(waiting.size() == 2, "a joiner asking twice waits once");
    WaitingJoiners::Entry entry{};
    check(waiting.next([](uintptr_t) { return true; }, entry) && entry.member == kSecond && entry.request == 0,
          "the oldest goes first, with its latest request");
    waiting.add(kSecond, 1);
    waiting.drop(kThird);
    check(waiting.size() == 1 && waiting.next([](uintptr_t member) { return member == kSecond; }, entry) && entry.member == kSecond,
          "a joiner that left is dropped");
    waiting.add(kFirst, 1);
    check(!waiting.next([](uintptr_t) { return false; }, entry) && waiting.size() == 0, "joiners that are no longer members are forgotten");
    for (uintptr_t member = 1; member <= kMaxWaiting + 3; ++member) waiting.add(member, 1);
    check(waiting.size() == kMaxWaiting, "the queue holds as many as the session has members");
    return g_failures == 0 ? 0 : 1;
}
