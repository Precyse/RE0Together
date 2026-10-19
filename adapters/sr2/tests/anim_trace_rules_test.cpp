// The anim set trace rules of milestone 14 (no game): the need table text, the anim headers that cannot be decoded and the chunk anim
// slot an address is in, and the three trace sites as data.
#include <cstdio>
#include <string>
#include <vector>

#include "../src/anim_trace_rules.h"
#include "../src/player_sites.h"

namespace {

using anim_trace_rules::NeedRow;
using anim_trace_rules::Slot;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

void theNeedTable() {
    const NeedRow rows[] = {{0x2BF, true, true}, {0x2C5, true, false}, {0, false, false}, {0x2FE, false, true}, {0x2C6, false, false}};
    check(anim_trace_rules::needTableText(rows, 5) == "{0x02BF 1 1, 0x02C5 1 0, 0x02FE 0 1}", "only the rows somebody needs: group, local byte, remote byte");
    check(anim_trace_rules::needTableText(rows + 2, 1) == "{}" && anim_trace_rules::needTableText(nullptr, 0) == "{}", "nothing needed: empty braces");
}

void theUndecodableHeader() {
    constexpr uint8_t kFlag = 0x40;
    check(anim_trace_rules::undecodable(0x2A0CD4C0, kFlag, 0x7E000003, 0xC72), "the crash of the dump: flag set, header offset 0x7E000003 past a size of 0xC72");
    check(!anim_trace_rules::undecodable(0x2A0CD4C0, kFlag, 0x20, 0xC72), "an offset inside the anim is fine");
    check(anim_trace_rules::undecodable(0x1000, kFlag, 0xC72, 0xC72), "an offset equal to the size is already outside");
    check(!anim_trace_rules::undecodable(0x2A0CD4C0, 0, 0x7E000003, 0xC72), "without the flag the offset is not read");
    check(!anim_trace_rules::undecodable(0, kFlag, 0x7E000003, 0xC72), "no data: the decode does not run");
}

void theSlots() {
    const Slot slots[] = {{0x28000000, 0x100000}, {0x2A0CD020, 0x100000}, {0x2B000000, 0x100000}};
    check(anim_trace_rules::slotOf(0x2A0CD4C0, slots, 3) == 1, "the dump's data 0x2A0CD4C0 is in chunk_anim1");
    check(anim_trace_rules::slotOf(0x28000000, slots, 3) == 0 && anim_trace_rules::slotOf(0x280FFFFF, slots, 3) == 0, "a slot holds its base and its last byte");
    check(anim_trace_rules::slotOf(0x28100000, slots, 3) == -1 && anim_trace_rules::slotOf(0x1000, slots, 3) == -1, "past the end or elsewhere: outside");
}

void theSites() {
    using partner_sites::Kind;
    const auto& rows = player_sites::kAnimTrace;
    check(std::size(rows) == 3 && rows[0].address == 0xA83F30 && rows[1].address == 0xA841C0 && rows[2].address == 0xCE4870, "AN1, AN2 and AN3 at the research addresses");
    bool hooks = true;
    for (const auto& row : rows) hooks = hooks && row.kind == Kind::HookEntry && partner_sites::parseHex(row.expected).size() == 8;
    check(hooks, "each is a hook of a function entry with 8 checked bytes");
    check(partner_sites::parseHex(rows[0].expected) == std::vector<uint8_t>{0x81, 0xEC, 0x28, 0x06, 0x00, 0x00, 0xA1, 0x50} &&
              partner_sites::parseHex(rows[1].expected) == std::vector<uint8_t>{0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x81, 0xEC} &&
              partner_sites::parseHex(rows[2].expected) == std::vector<uint8_t>{0x81, 0xEC, 0x34, 0x03, 0x00, 0x00, 0xA1, 0x50},
          "the entry bytes the research read");
    check(rows[0].thunk != rows[1].thunk && rows[1].thunk != rows[2].thunk && rows[0].thunk != rows[2].thunk, "each has its own thunk");
}

}  // namespace

int main() {
    theNeedTable();
    theUndecodableHeader();
    theSlots();
    theSites();
    return g_failures == 0 ? 0 : 1;
}
