#include "build_gate.h"

#include <windows.h>

#include "log.h"
#include "paths.h"

namespace {

constexpr uint32_t kHeaderPage = 0x1000;  // the PE headers of the loaded exe

}  // namespace

namespace build_gate {

uint32_t timeDateStampOf(const void* image, uint32_t availableBytes) {
    if (availableBytes < sizeof(IMAGE_DOS_HEADER)) return 0;
    const auto* dos = static_cast<const IMAGE_DOS_HEADER*>(image);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0) return 0;
    if (static_cast<uint64_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) > availableBytes) return 0;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(static_cast<const uint8_t*>(image) + dos->e_lfanew);
    return nt->Signature == IMAGE_NT_SIGNATURE ? nt->FileHeader.TimeDateStamp : 0;
}

bool checkRunningGame() {
    const uint32_t stamp = timeDateStampOf(GetModuleHandleW(nullptr), kHeaderPage);
    if (stamp != kSupportedTimeDateStamp) {
        logger::write("build_gate: DD2.exe stamp 0x%08X, supported 0x%08X: co-op off", stamp, kSupportedTimeDateStamp);
        return false;
    }
    if (GetFileAttributesW((gameDirectory() + kDd2getherMarker).c_str()) != INVALID_FILE_ATTRIBUTES) {
        logger::write("build_gate: conflict: DD2gether present (reframework\\autorun\\coop): co-op off");
        return false;
    }
    logger::write("build_gate: DD2.exe stamp 0x%08X supported", stamp);
    return true;
}

}  // namespace build_gate
