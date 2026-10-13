#include "code_check.h"

#include <windows.h>

#include <cstdint>
#include <vector>

#include "code_patch.h"
#include "log.h"
#include "memory.h"
#include "sr2_addresses.h"

namespace {

const std::vector<CodePatch>& expectedCode() {
    static const std::vector<CodePatch> list = {
        {"net_set_mode", sr2::kNetSetMode, {0xA1, 0x60, 0x8B, 0x52, 0x02, 0x83, 0xEC, 0x18, 0x39, 0x44, 0x24, 0x1C}},
        {"session_pool_setup", sr2::kSessionPoolSetup, {0x53, 0x55, 0x56, 0x57, 0x8B, 0x3D, 0xA8, 0x23, 0xFA, 0x00}},
        {"player_slot_allocator", sr2::kPlayerSlotAllocator, {0x53, 0x55, 0x56, 0x57, 0x8B, 0x3D, 0xD0, 0x03, 0x17, 0x02}},
        {"join_queue_add", sr2::kJoinQueueAdd, {0x51, 0x53, 0x8B, 0x5C, 0x24, 0x10, 0x55, 0x8B, 0x6C, 0x24, 0x10}},
        // mov [kMaxSessionPlayers], ebp
        {"store_max_session_players", sr2::kStoreMaxSessionPlayers, {0x89, 0x2D, 0x34, 0xC4, 0xE8, 0x00}},
        // mov [kMaxPlayers], ebp
        {"store_max_players", sr2::kStoreMaxPlayers, {0x89, 0x2D, 0x38, 0xC4, 0xE8, 0x00}},
        // push <size>: the shipped sizes of the two co-op player pools (0x228000 and 0x1071000)
        {"coop_player_cpu_size", sr2::kCoopPlayerCpuSizePush, {0x68, 0x00, 0x80, 0x22, 0x00}},
        {"coop_player_gpu_size", sr2::kCoopPlayerGpuSizePush, {0x68, 0x00, 0x10, 0x07, 0x01}},
        // push 0x5ADBB0: the shipped size of coop_allocators
        {"coop_allocators_size", sr2::kCoopAllocatorsSizePush, {0x68, 0xB0, 0xDB, 0x5A, 0x00}},
        // mov [0xE8C43C], 150: the npc limit the N=8 npc count is derived from
        {"coop_npc_limit_store", sr2::kCoopNpcLimitStore, {0xC7, 0x05, 0x3C, 0xC4, 0xE8, 0x00, 0x96, 0x00, 0x00, 0x00}},
        // call kActivateAllocatorSet
        {"activate_coop_sets_call", sr2::kActivateCoopSetsCall, {0xE8, 0xBB, 0xFA, 0xFF, 0xFF}},
        // mov edi, "coop-mp" (kSetNameCoopMp) when entering and when leaving co-op
        {"coop_set_name_activate", sr2::kLoadCoopSetNameActivate, {0xBF, 0xB0, 0x62, 0xE2, 0x00}},
        {"coop_set_name_deactivate", sr2::kLoadCoopSetNameDeactivate, {0xBF, 0xB0, 0x62, 0xE2, 0x00}},
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

}  // namespace

bool gameMatchesBuild() {
    bool ok = timeStampMatches();
    ok = codeMatches(expectedCode()) && ok;
    logger::write(ok ? "code check: exe matches the supported build" : "code check: exe differs, nothing will be patched");
    return ok;
}
