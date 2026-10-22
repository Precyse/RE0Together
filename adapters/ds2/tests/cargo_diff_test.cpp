// cargo_diff: comparing two multisets of cargo pieces (no game).
#include <cstdint>
#include <cstdio>
#include <vector>

#include "../src/cargo_diff.h"

namespace {

using namespace cargo_diff;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

struct Piece {
    uint32_t type;
    uint64_t orderId;
    uint64_t secondId;
    uint8_t category;
    float durability;
    uint64_t handle;
};

Piece plain(uint32_t type, uint64_t handle, float durability = 0) { return {type, 0, 0, 0, durability, handle}; }
Piece order(uint32_t type, uint64_t orderId, uint64_t handle) { return {type, orderId, 0, 7, 900.0f, handle}; }

Handles handlesOf(const std::vector<Piece>& pieces) {
    Handles out;
    for (const Piece& piece : pieces) out[keyOf(piece)].push_back(piece.handle);
    return out;
}

}  // namespace

int main() {
    const std::vector<Piece> three = {plain(10, 1), plain(10, 2), plain(10, 3)};
    const std::vector<Piece> one = {plain(10, 9)};

    check(extra(handlesOf(three), countsOf(one)).size() == 2, "3 held, 1 wanted: two extra");
    check(missing(countsOf(three), countsOf(one)).empty(), "3 held, 1 wanted: nothing missing");
    check(missing(countsOf(one), countsOf(three)).at({10, 0}) == 2, "1 held, 3 wanted: two missing");
    check(extra(handlesOf(one), countsOf(three)).empty(), "1 held, 3 wanted: nothing extra");

    const std::vector<Piece> held = {order(20, 0x100, 1), order(20, 0x200, 2)};
    const std::vector<Piece> wanted = {order(20, 0x200, 0)};
    const std::vector<uint64_t> surplus = extra(handlesOf(held), countsOf(wanted));
    check(surplus.size() == 1 && surplus[0] == 1, "order pieces of one kind are told apart by their order id");
    check(missing(countsOf(held), countsOf(wanted)).empty(), "the order piece that is wanted is held");

    const std::vector<Piece> mixedHave = {plain(10, 1), plain(10, 2), plain(11, 3)};
    const std::vector<Piece> mixedWant = {plain(10, 0), plain(12, 0), plain(12, 0)};
    const std::vector<uint64_t> mixedExtra = extra(handlesOf(mixedHave), countsOf(mixedWant));
    const Counts mixedMissing = missing(countsOf(mixedHave), countsOf(mixedWant));
    check(mixedExtra.size() == 2 && mixedExtra[0] == 2 && mixedExtra[1] == 3, "extra and missing at once: the surplus handles");
    check(mixedMissing.size() == 1 && mixedMissing.at({12, 0}) == 2, "extra and missing at once: the shortfall");

    check(extra(handlesOf(three), {}).size() == 3, "an empty want empties the locker");
    check(missing({}, {}).empty() && extra({}, {}).empty(), "nothing held and nothing wanted differ in nothing");
    check(countsOf(handlesOf(three)).at({10, 0}) == 3, "counts of grouped handles");

    std::vector<Piece> a = {plain(11, 1, 0.5f), plain(10, 2), plain(10, 3, 0.25f)};
    std::vector<Piece> b = {plain(10, 9, 0.25f), plain(10, 8), plain(11, 7, 0.5f)};
    canonicalOrder(a);
    canonicalOrder(b);
    check(sameContents(a, b), "the same pieces in another order are the same contents");
    b[0].durability = 0.1f;
    canonicalOrder(b);
    check(!sameContents(a, b), "a changed durability is a different content");
    check(!sameContents(a, std::vector<Piece>{plain(10, 1)}), "lists of different sizes differ");

    std::printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
    return g_failures ? 1 : 0;
}
