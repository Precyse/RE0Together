#pragma once
// DS2-internal: the cutscene log (adapter.ini cutscene_log=1), see ds2/cutscene_log.cpp.
#include <cstdint>

#include "ds2/sequence_info.h"

namespace cutscene_log {

// Start-up: turns the log on and starts the game-state bits log.
void enable();

// One shared Sequence start the cutscene hook saw; `decision` says whether it was held or let start.
void onStart(uintptr_t sequence, const sequence_info::Info& info, const char* decision);

// A Sequence start whose resource could not be read as a SequenceResource: the raw walk, so the walk can be corrected.
void onUnread(uintptr_t sequence);

}  // namespace cutscene_log
