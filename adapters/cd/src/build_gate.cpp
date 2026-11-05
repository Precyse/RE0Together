#include "build_gate.h"

#include <windows.h>

#include <cstring>

#include "log.h"
#include "safe_read.h"

namespace {

constexpr uint32_t kHeaderPage = 0x1000;  // the PE headers of the loaded exe

}  // namespace

namespace build_gate {

Identity identityOf(const void* image, uint32_t availableBytes) {
    const auto* bytes = static_cast<const uint8_t*>(image);
    if (availableBytes < sizeof(IMAGE_DOS_HEADER)) return {};
    const auto* dos = static_cast<const IMAGE_DOS_HEADER*>(image);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0) return {};
    if (static_cast<uint64_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) > availableBytes) return {};
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(bytes + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return {};
    return {nt->FileHeader.TimeDateStamp, nt->OptionalHeader.ImageBase};
}

bool supported(const Identity& found) {
    return found.timeDateStamp == kSupported.timeDateStamp && found.imageBase == kSupported.imageBase;
}

bool checkRunningGame() {
    const auto module = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const Identity found = identityOf(reinterpret_cast<const void*>(module), kHeaderPage);
    const bool ok = supported(found) && module == kSupported.imageBase;
    logger::write("build_gate: exe stamp=0x%08X base=0x%llX loaded at 0x%llX: %s", found.timeDateStamp,
                  static_cast<unsigned long long>(found.imageBase), static_cast<unsigned long long>(module),
                  ok ? "supported" : "build_mismatch, co-op off");
    return ok;
}

bool siteMatches(const cd_sites::Site& site, const uint8_t* live) {
    return std::memcmp(site.bytes.data(), live, cd_sites::kSiteBytes) == 0;
}

bool setIntact(const char* set) {
    bool intact = true;
    for (const cd_sites::Site& site : cd_sites::kAll) {
        if (std::strcmp(site.set, set) != 0) continue;
        uint8_t live[cd_sites::kSiteBytes] = {};
        if (!safe_read::bytes(site.address, live, sizeof(live)) || !siteMatches(site, live)) {
            logger::write("build_gate: %s at 0x%llX: bytes differ (%02X %02X %02X %02X ...)", site.name,
                          static_cast<unsigned long long>(site.address), live[0], live[1], live[2], live[3]);
            intact = false;
        }
    }
    return intact;
}

}  // namespace build_gate
