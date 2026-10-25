// Checks the text override table of the game's text lookup (no game). Exit 0 when every check passes.
#include <cstdio>

#include "../src/gui_text_rule.h"

namespace {

constexpr int kTableSlots = 19;
constexpr uint32_t kOwnIdLimit = 0x100;  // the game's own message tables are far smaller than this

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

void testGameIdsStayAlone() {
    for (int slot = 0; slot < kTableSlots; ++slot) {
        for (uint32_t id = 0; id < kOwnIdLimit; ++id) {
            if (!gui_text_rule::find(slot, id)) continue;
            std::printf("FAIL: slot %d id 0x%x is relabelled but nothing should be yet\n", slot, id);
            ++g_failures;
        }
    }
    check(!gui_text_rule::find(0, 0xffffffffu), "the no-text id is not touched");
}

void testEntries() {
    for (const gui_text_rule::Entry& entry : gui_text_rule::kEntries) {
        check(entry.text && entry.text[0] != '\0', "every entry has text");
        check(entry.slot >= 0 && entry.slot < kTableSlots, "every entry names a table slot");
        check(entry.id >= kOwnIdLimit, "every new id is above the tables' own ids");
    }
}

}  // namespace

int main() {
    testGameIdsStayAlone();
    testEntries();
    if (g_failures == 0) std::printf("PASS\n");
    return g_failures == 0 ? 0 : 1;
}
