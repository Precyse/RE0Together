#pragma once
// The guard before a replayed stage branch (cd design progression.md 4.2; static_s14_s16.md 3): the client indexes a
// stage's branch table without a bounds check, so an out-of-range branch crashes it. The stage manager is taken from the
// game's own stage lookup (rcx of 0x142156CA0, a hook that only records it); the guard then finds the stage by memory
// reads alone (every bucket of the manager's table, entry +0x08 instance, +0x10 stage; a dead stage is skipped) and reads
// its branch table ([[stage+0x60]+0x38]: +0x08 entries [V], +0x10 count [I]).
#include <cstdint>
#include <optional>

namespace stage_guard {

// Installs the lookup hook (set "story"); false when the site bytes differ.
bool install();

// Forgets the manager (a load starts: the field's manager goes away with the field).
void reset();

struct Table {
    uintptr_t entries = 0;
    uint32_t count = 0;
};

// The branch table of our own instance, or null (no manager seen, no such live stage, or unreadable).
std::optional<Table> table(uint64_t instance);

// The branch is below the table's count and its entry reads as the game reads it.
bool branchOk(const Table& table, uint32_t branch);

}  // namespace stage_guard
