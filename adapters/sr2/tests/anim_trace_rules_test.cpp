// The anim set trace rules of milestones 14 and 16 W1 (no game): the need table text and where the code keeps the table, the other sets
// in a slot, the anim headers that cannot be decoded and the chunk anim slot an address is in, and the three trace sites as data.
#include <cstdio>
#include <string>
#include <vector>

#include "../src/anim_trace_rules.h"
#include "../src/player_sites.h"
#include "check.h"

namespace {

using anim_trace_rules::NeedRow;
using anim_trace_rules::Slot;

void theNeedTable() {
    const NeedRow rows[] = {{0x2BF, true, 0}, {0x2C5, false, 0x06}, {0, false, 0}, {0x2FE, true, 0x01}, {0x2C6, false, 0}};
    check(anim_trace_rules::needTableText(rows, 5) == "{0x02BF 1 0x00, 0x02C5 0 0x06, 0x02FE 1 0x01}", "only the rows somebody needs: group, local byte, remote mask in hex");
    check(anim_trace_rules::needTableText(rows + 2, 1) == "{}" && anim_trace_rules::needTableText(nullptr, 0) == "{}", "nothing needed: empty braces");
    check(anim_trace_rules::needTableText(rows + 1, 1) == "{0x02C5 0 0x06}", "two guests needing one group: one row with both bits");
}

void theNeedTableFromCode() {
    using anim_trace_rules::needTableFromCode;
    const auto shipped = needTableFromCode(sr2::kAnimNeedTable, sr2::kAnimNeedTableEnd);
    check(shipped.base == sr2::kAnimNeedTable && shipped.rows == 8, "the shipped immediates give the shipped table: 8 rows");
    constexpr uint32_t kMoved = 0x3A00000;
    const auto moved = needTableFromCode(kMoved, kMoved + 14 * sr2::kAnimNeedRowBytes);
    check(moved.base == kMoved && moved.rows == 14, "a moved table: the new base and 14 rows");
    check(needTableFromCode(0, 0).rows == 0 && needTableFromCode(kMoved, kMoved).rows == 0 && needTableFromCode(kMoved + 0xD0, kMoved).rows == 0,
          "an unreadable pair, an empty table and an end before the base give no rows");
    check(needTableFromCode(kMoved, kMoved + 14 * sr2::kAnimNeedRowBytes + 1).rows == 0, "a pair that is not a whole number of rows gives no rows");
}

void theOtherSets() {
    using anim_trace_rules::StreamEntry;
    constexpr uint32_t kDescriptor = 0x2771894;
    constexpr uint32_t kOtherDescriptor = 0x27718EC;
    constexpr uint32_t kInUse = 1;
    const StreamEntry entries[] = {{kInUse, kDescriptor, 0x2C5},    {0, kDescriptor, 0x2C6}, {3, kDescriptor, 0x2C7},
                                   {4, kDescriptor, 0x2C8},         {kInUse, kDescriptor, 0x2C9}, {2, kOtherDescriptor, 0x2CA},
                                   {kInUse, kDescriptor, 0x2C5}};
    const auto others = anim_trace_rules::otherSetsInSlot(entries, kDescriptor, 0x2C5);
    check(others.size() == 1 && others[0] == 0x2C9, "the free states 0, 3 and 4, the same group and another slot's sets are skipped; a different group in the slot is listed");
    check(anim_trace_rules::otherSetsInSlot(entries, kDescriptor, 0x2C9).size() == 2 && anim_trace_rules::otherSetsInSlot({}, kDescriptor, 0x2C5).empty(),
          "the group being unloaded is whichever the call names (a duplicate load of another group is listed twice), and no entries list nothing");
    check(anim_trace_rules::groupsText({}) == "{}" && anim_trace_rules::groupsText({0x2C5}) == "{0x02C5}" && anim_trace_rules::groupsText({0x2C5, 0x2C6}) == "{0x02C5, 0x02C6}",
          "the list as the trace prints it");
    check(anim_trace_rules::syncIdText(0, 0x1234) == " id 0x00001234" && anim_trace_rules::syncIdText(1, 0x1234) == " id 0x00001234" && anim_trace_rules::syncIdText(2, 0x1234).empty(),
          "a request and an answer name the set's id, a drop has none");
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
    theNeedTableFromCode();
    theOtherSets();
    theUndecodableHeader();
    theSlots();
    theSites();
    return g_failures == 0 ? 0 : 1;
}
