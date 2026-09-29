#include <cstdio>

#include "../src/flag_diff.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    ++g_failures;
    std::printf("FAIL: %s\n", what);
}

}  // namespace

int main() {
    flag_diff::Words from{};
    flag_diff::Words to{};
    from[3] = 0b1010;
    to[3] = 0b0110;
    to[70] = 0x80000000u;

    const auto changes = flag_diff::diff(from, to);
    check(changes.size() == 2, "one change per differing word");
    check(changes[0].word == 3 && changes[0].set == 0b0100 && changes[0].clear == 0b1000, "set and clear bits");
    check(changes[1].word == 70 && changes[1].set == 0x80000000u && changes[1].clear == 0, "last word");

    flag_diff::Words applied = from;
    for (const auto& change : changes) flag_diff::apply(applied, change);
    check(applied == to, "applying the diff reproduces the target");

    flag_diff::Words untouched = to;
    flag_diff::apply(untouched, {static_cast<uint16_t>(flag_diff::kWords), 0, 1, 0});
    check(untouched == to, "out-of-range word ignored");

    check(flag_diff::diff(to, to).empty(), "no changes between equal words");
    if (g_failures == 0) std::printf("flag_diff_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
