#pragma once
// DS2-internal: the cutscene log (adapter.ini cutscene_log=1), see ds2/cutscene_log.cpp.
namespace cutscene_log {

// Start-up: the Sequence start hook and the game-state bits log.
void installEarly();

}  // namespace cutscene_log
