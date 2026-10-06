#pragma once
// DS2-internal: synced cutscenes (see ds2/cutscene.cpp).
#include <cstdint>

namespace cutscene {

// Start-up: detours on the Sequence start 0x1403f30f0 and stop 0x1403f4750 (always, the cutscene log reads them) and, with
// `sync`, the roles: the host holds a shared cutscene until the guests are ready, a guest plays only announced ones and
// cannot skip them.
// `shareMinFrames` (test only, 0 off) shares every Sequence at least that long, so the hold, START, READY and GO can be
// exercised with any network the test commands start.
void installEarly(bool sync, uint32_t shareMinFrames);

}  // namespace cutscene
