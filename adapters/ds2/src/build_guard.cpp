#include "build_guard.h"

#include <windows.h>

#include "log.h"

namespace build_guard {

Identity identityOf(const void* image, uint32_t availableBytes) {
    const auto* bytes = static_cast<const uint8_t*>(image);
    if (availableBytes < sizeof(IMAGE_DOS_HEADER)) return {};
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0) return {};
    const auto ntOffset = static_cast<uint32_t>(dos->e_lfanew);
    if (ntOffset > availableBytes || availableBytes - ntOffset < sizeof(IMAGE_NT_HEADERS64)) return {};
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(bytes + ntOffset);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return {};
    return {nt->FileHeader.TimeDateStamp, nt->OptionalHeader.SizeOfImage};
}

bool supported(const Identity& found) {
    return found.timeDateStamp == kSupported.timeDateStamp && found.sizeOfImage == kSupported.sizeOfImage;
}

bool checkRunningGame() {
    const auto* image = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
    const Identity found = identityOf(image, sizeof(IMAGE_DOS_HEADER) + 0x400);  // the headers sit in the first page; no hooks yet, so the exe is untouched
    const bool ok = supported(found);
    logger::write("build_guard: exe stamp %08x size %08x, %s", found.timeDateStamp, found.sizeOfImage,
                  ok ? "supported" : "UNSUPPORTED, no hooks installed");
    return ok;
}

}  // namespace build_guard
