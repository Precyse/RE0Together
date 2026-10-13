#include "code_check.h"

#include <windows.h>

#include <cstdint>
#include <vector>

#include "byte_pattern.h"
#include "log.h"
#include "memory.h"
#include "sr2_addresses.h"

namespace {

struct CodePattern {
    const char* name;
    uintptr_t address;
    std::vector<uint8_t> bytes;
};

const std::vector<CodePattern>& patterns() {
    static const std::vector<CodePattern> list = {
        {"net_set_mode", sr2::kNetSetMode, {0xA1, 0x60, 0x8B, 0x52, 0x02, 0x83, 0xEC, 0x18, 0x39, 0x44, 0x24, 0x1C}},
        {"session_pool_setup", sr2::kSessionPoolSetup, {0x53, 0x55, 0x56, 0x57, 0x8B, 0x3D, 0xA8, 0x23, 0xFA, 0x00}},
        {"remote_player_getter", sr2::kRemotePlayerGetter, {0xA0, 0x49, 0x8B, 0x52, 0x02, 0x84, 0xC0, 0x74}},
        {"player_slot_allocator", sr2::kPlayerSlotAllocator, {0x53, 0x55, 0x56, 0x57, 0x8B, 0x3D, 0xD0, 0x03, 0x17, 0x02}},
        {"join_queue_add", sr2::kJoinQueueAdd, {0x51, 0x53, 0x8B, 0x5C, 0x24, 0x10, 0x55, 0x8B, 0x6C, 0x24, 0x10}},
        // mov [kMaxSessionPlayers], ebp
        {"store_max_session_players", sr2::kStoreMaxSessionPlayers, {0x89, 0x2D, 0x34, 0xC4, 0xE8, 0x00}},
        // mov [kMaxPlayers], ebp
        {"store_max_players", sr2::kStoreMaxPlayers, {0x89, 0x2D, 0x38, 0xC4, 0xE8, 0x00}},
    };
    return list;
}

bool timeStampMatches() {
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS32 nt{};
    if (!readMemory(base, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        !readMemory(base + dos.e_lfanew, &nt, sizeof(nt)) || nt.Signature != IMAGE_NT_SIGNATURE) {
        logger::write("code check: exe headers unreadable");
        return false;
    }
    if (nt.FileHeader.TimeDateStamp != sr2::kExeTimeDateStamp) {
        logger::write("code check: exe time stamp 0x%08X, expected 0x%08X", nt.FileHeader.TimeDateStamp,
                      sr2::kExeTimeDateStamp);
        return false;
    }
    return true;
}

bool patternMatches(const CodePattern& pattern) {
    std::vector<uint8_t> actual(pattern.bytes.size());
    if (!readMemory(pattern.address, actual.data(), actual.size())) {
        logger::write("code check: %s at 0x%X unreadable", pattern.name, static_cast<unsigned>(pattern.address));
        return false;
    }
    const auto bad = firstMismatch(actual.data(), pattern.bytes.data(), actual.size());
    if (!bad) return true;
    logger::write("code check: %s at 0x%X differs at byte %zu (found %02X, expected %02X)", pattern.name,
                  static_cast<unsigned>(pattern.address), *bad, actual[*bad], pattern.bytes[*bad]);
    return false;
}

}  // namespace

bool gameMatchesBuild() {
    bool ok = timeStampMatches();
    for (const CodePattern& pattern : patterns()) ok = patternMatches(pattern) && ok;
    logger::write(ok ? "code check: exe matches the supported build" : "code check: exe differs, nothing will be patched");
    return ok;
}
