// gear_snapshot: the multiset difference and the file round trip (no game).
#include <cstdio>

#include "../src/gear_snapshot.h"

namespace {

using namespace gear_snapshot;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

}  // namespace

int main() {
    const Items base = {{kBackpackSlot, 100, 1, 0}, {kBackpackSlot, 100, 1, 0}, {10, 7, 0, 0}};
    const Items held = {{kBackpackSlot, 100, 1, 0}, {kBackpackSlot, 100, 1, 0}, {kBackpackSlot, 100, 1, 0.5f},
                        {10, 7, 0, 0},              {10, 8, 0, 0}};

    const Items gained = minus(held, base);
    check(gained.size() == 2, "a third piece of a kind and a new weapon are what was gained");
    check(gained[0].type == 100 && gained[0].durability == 0.5f, "the surplus piece keeps its durability");
    check(gained[1].slot == 10 && gained[1].type == 8, "a different kind in the same slot counts");

    check(minus(base, held).empty(), "nothing is missing when everything is held");
    check(minus(held, held).empty(), "a list minus itself is empty");
    check(minus(held, {}).size() == held.size(), "against nothing, everything is extra");
    check(minus({{4, 5, 0, 0}}, {{kBackpackSlot, 5, 0, 0}}).size() == 1, "the same kind in another slot is another piece");

    Items parsed;
    check(parse(format(gained), parsed) && parsed.size() == 2 && parsed[0].type == 100 && parsed[0].durability == 0.5f &&
              parsed[1].slot == 10 && parsed[1].type == 8,
          "format then parse gives the list back");
    check(parse(format({}), parsed) && parsed.empty(), "an empty snapshot round trips");

    Items untouched = {{1, 2, 3, 4}};
    check(!parse("ds2-gear 2\n", untouched) && untouched.size() == 1, "another version is refused");
    check(!parse("ds2-gear 1\n10 7 0\n", untouched) && untouched.size() == 1, "a short line is refused");
    check(!parse("ds2-gear 1\n300 7 0 0\n", untouched), "a slot above 255 is refused");
    check(!parse("", untouched), "an empty file is refused");

    std::printf(g_failures ? "FAILED\n" : "PASS\n");
    return g_failures ? 1 : 0;
}
