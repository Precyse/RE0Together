// Checks the equip sync rule (no game). Exit 0 when every check passes.
#include <cstdio>
#include <cstring>

#include "../src/equip_rule.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

void setSlot(uint8_t (&block)[equip_rule::kBlockSize], uint32_t slot) {
    std::memcpy(block + equip_rule::kEquippedOffset, &slot, sizeof(slot));
}

}  // namespace

int main() {
    uint8_t before[equip_rule::kBlockSize] = {};
    uint8_t after[equip_rule::kBlockSize] = {};
    setSlot(before, 4);
    setSlot(after, 4);
    check(equip_rule::equippedSlot(before) == 4, "equipped slot is read from its offset");
    check(!equip_rule::needsRefresh(before, after), "same slot needs no refresh");
    setSlot(after, 2);
    check(equip_rule::needsRefresh(before, after), "another slot needs a refresh");
    setSlot(after, equip_rule::kNoSlot);
    check(equip_rule::needsRefresh(before, after), "unequipping needs a refresh");
    setSlot(before, equip_rule::kNoSlot);
    check(!equip_rule::needsRefresh(before, after), "none to none needs no refresh");
    before[0] = 1;
    after[0] = 9;
    check(!equip_rule::needsRefresh(before, after), "a changed item count alone needs no refresh");
    if (g_failures == 0) std::printf("equip_rule_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
