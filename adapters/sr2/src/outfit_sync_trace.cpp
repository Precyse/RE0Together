#include "outfit_sync_trace.h"

#include <string>

#include "log.h"
#include "memory.h"
#include "script_trace_rules.h"
#include "thunk_frame.h"

namespace {

constexpr size_t kPayloadArg = 0;
constexpr uintptr_t kKindOffset = 0;      // byte: nonzero outfit, 0 item (act 0xC); 2 weapon slots (hud 0x17)
constexpr uintptr_t kNameOffset = 1;      // act 0xC: the item or outfit name, NUL terminated
constexpr uintptr_t kEnableOffset = 1;    // hud 0x17: byte, nonzero = enable
constexpr uintptr_t kIndexGap = 2;        // act 0xC outfit: the index dword is at payload + name length + 2 (the kind byte, the name, its NUL)
constexpr uint8_t kItemKind = 0;
constexpr uint8_t kWeaponSlotsKind = 2;
constexpr size_t kNameScanLimit = 256;    // characters scanned to find the end of a name
constexpr size_t kNameShownLimit = 64;    // characters of it that are logged

void* g_outfitOriginal = nullptr;
void* g_revertOriginal = nullptr;
void* g_weaponSlotsOriginal = nullptr;

// OT1: the clothes or item that another machine sent for the local player.
void __cdecl traceOutfitReceived(GuestFrame* frame) {
    const uintptr_t payload = frame->args[kPayloadArg];
    char name[kNameScanLimit + 1];
    const size_t length = readCString(payload + kNameOffset, name, sizeof(name));
    const std::string shown = script_trace_rules::clipped(name, length, kNameShownLimit);
    if (readByte(payload + kKindOffset) != kItemKind) {
        const int index = static_cast<int>(readDword(payload + length + kIndexGap));
        logger::write("trace: outfit sync: received outfit \"%s\" index %d, applied to the local player", shown.c_str(), index);
    } else {
        logger::write("trace: outfit sync: received item \"%s\"", shown.c_str());
    }
}

// OT2: the host (or another machine) restored the clothes.
void __cdecl traceRevertReceived(GuestFrame*) { logger::write("trace: outfit sync: received clothes revert"); }

// OT3: the hud receiver also handles other kinds; only kind 2 locks or unlocks the weapon slots.
void __cdecl traceWeaponSlotsReceived(GuestFrame* frame) {
    const uintptr_t payload = frame->args[kPayloadArg];
    if (readByte(payload + kKindOffset) != kWeaponSlotsKind) return;
    logger::write("trace: outfit sync: received weapon slots %s", readByte(payload + kEnableOffset) ? "enable" : "disable");
}

TRACE_ENTRY_STUB(outfitReceivedStub, traceOutfitReceived, g_outfitOriginal)
TRACE_ENTRY_STUB(revertReceivedStub, traceRevertReceived, g_revertOriginal)
TRACE_ENTRY_STUB(weaponSlotsReceivedStub, traceWeaponSlotsReceived, g_weaponSlotsOriginal)

}  // namespace

namespace outfit_sync_trace {

partner_thunks::Binding bind(partner_sites::Thunk thunk) {
    using partner_sites::Thunk;
    switch (thunk) {
        case Thunk::TraceOutfitReceived: return {reinterpret_cast<void*>(&outfitReceivedStub), &g_outfitOriginal};
        case Thunk::TraceRevertReceived: return {reinterpret_cast<void*>(&revertReceivedStub), &g_revertOriginal};
        case Thunk::TraceWeaponSlotsReceived: return {reinterpret_cast<void*>(&weaponSlotsReceivedStub), &g_weaponSlotsOriginal};
        default: return {nullptr, nullptr};
    }
}

}  // namespace outfit_sync_trace
