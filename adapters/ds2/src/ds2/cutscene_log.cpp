// DEATH STRANDING 2: cutscene log (adapter.ini cutscene_log=1, off by default): every Sequence start 0x1403f30f0 with its
// resource UUID, category and stop frame, and every change of the DSGameState bits (the cutscene states), for the live
// checks of docs/DS2_ROADMAP.md (synced cutscenes).
#include "ds2/cutscene_log.h"

#include <windows.h>

#include <cstdint>

#include "decima/safe_read.h"
#include "ds2/engine.h"
#include "ds2/sim_tick.h"
#include "hooks.h"
#include "log.h"

namespace {

constexpr uintptr_t kSequenceStart = 0x1403f30f0;
constexpr uintptr_t kGameStateGlobal = 0x14623E4C0;
constexpr uintptr_t kBitsLow = 0x160, kBitsHigh = 0x168;

using StartFn = void (*)(uintptr_t);
StartFn g_start = nullptr;

void startDetour(uintptr_t sequence) {
    const uintptr_t resource = decima::readPointer(decima::readPointer(sequence + 0x68));
    uint8_t uuid[16] = {};
    decima::safeCopy(uuid, resource + 0x10, sizeof(uuid));
    uint32_t nameHash = 0;
    uint8_t category = 0;
    decima::safeRead(resource + 0xB8, nameHash);
    decima::safeRead(resource + 0xC3, category);
    logger::write("cutscene_log: Sequence start %p frame %d stop %d state %04x category %u hash %08x uuid %02x%02x%02x%02x%02x%02x%02x%02x",
                  reinterpret_cast<void*>(sequence), ds2::field<int32_t>(sequence, 0x34C),
                  ds2::field<int32_t>(sequence, 0x33C), ds2::field<uint16_t>(sequence, 0x340), category, nameHash, uuid[0],
                  uuid[1], uuid[2], uuid[3], uuid[4], uuid[5], uuid[6], uuid[7]);
    g_start(sequence);
}

void tick() {
    static ULONGLONG last = 0;
    static uint64_t lastLow = ~0ull, lastHigh = ~0ull;
    const ULONGLONG now = GetTickCount64();
    if (now - last < 250) return;
    last = now;
    const uintptr_t state = decima::readPointer(ds2::at(kGameStateGlobal));
    if (!state) return;
    const uint64_t low = ds2::field<uint64_t>(state, kBitsLow), high = ds2::field<uint64_t>(state, kBitsHigh);
    if (low != lastLow || high != lastHigh) {
        lastLow = low;
        lastHigh = high;
        logger::write("cutscene_log: game state bits low %016llx high %016llx", static_cast<unsigned long long>(low),
                      static_cast<unsigned long long>(high));
    }
}

}  // namespace

namespace cutscene_log {
void installEarly() {
    hooks::install("cutscene sequence start", ds2::at(kSequenceStart), reinterpret_cast<void*>(&startDetour),
                   reinterpret_cast<void**>(&g_start));
    sim_tick::add(&tick, "cutscene log");
}
}  // namespace cutscene_log
