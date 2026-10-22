// locker_table: the guest's moves waiting for an answer and the host's rate limit (no game).
#include <cstdio>

#include "../src/locker_table.h"

namespace {

using namespace locker_table;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

locker_wire::LockerMove moveIn(uint32_t key, uint8_t kind) {
    locker_wire::LockerMove move{};
    move.key = key;
    move.slotKind = kind;
    move.direction = locker_wire::kDirectionPut;
    return move;
}

}  // namespace

int main() {
    PendingMoves table;
    const locker_wire::LockerMove first = table.add(moveIn(0xAA, game::kSlotPublic), 1000);
    const locker_wire::LockerMove second = table.add(moveIn(0xBB, game::kSlotPublic), 1000);
    check(first.requestId == 1 && second.requestId == 2, "request ids count up from 1");
    check(table.pending(0xAA, game::kSlotPublic), "a locker with a move waiting is pending");
    check(!table.pending(0xAA, game::kSlotPublicReceived), "the other kind of the same key is not");
    check(!table.pending(0xCC, game::kSlotPublic), "an untouched locker is not");

    const auto resolved = table.resolve(first.requestId);
    check(resolved && resolved->key == 0xAA, "a result returns its move");
    check(!table.pending(0xAA, game::kSlotPublic), "a result clears the wait");
    check(!table.resolve(first.requestId), "a second result for the same request is ignored");
    check(!table.resolve(99), "a result for an unknown request is ignored");

    table.expire(1000 + kTimeoutMs);
    check(table.pending(0xBB, game::kSlotPublic) && table.expiredTotal() == 0, "a move at the timeout is still waiting");
    table.expire(1000 + kTimeoutMs + 1);
    check(!table.pending(0xBB, game::kSlotPublic) && table.expiredTotal() == 1, "a move past the timeout is cleared and counted");

    const locker_wire::LockerMove third = table.add(moveIn(0xAA, game::kSlotPublic), 50000);
    check(third.requestId == 3, "ids keep counting after moves expired");
    table.clear();
    check(table.size() == 0, "clear forgets every wait");

    MoveLimiter limiter;
    bool allAllowed = true;
    for (uint32_t i = 0; i < kMaxMovesPerSecond; ++i) allAllowed = allAllowed && limiter.allow(5'000'000);
    check(allAllowed, "20 moves in a second are allowed");
    check(!limiter.allow(5'500'000), "the 21st in the same second is refused");
    check(limiter.allow(6'000'001), "a new second starts a new count");

    std::printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
    return g_failures ? 1 : 0;
}
