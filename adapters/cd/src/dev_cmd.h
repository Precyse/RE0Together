#pragma once
// Dev commands (adapter.ini dev_commands=1 only): one command per line in coop\cmd.txt, read and deleted once a
// second. `marker` sends CD_MARKER.
#include <cstdint>

namespace dev_cmd {

// Main tick only.
void tick(uint64_t nowMs);

}  // namespace dev_cmd
