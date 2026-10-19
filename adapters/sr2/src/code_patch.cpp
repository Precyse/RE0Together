#include "code_patch.h"

#include <windows.h>

#include <cstring>

#include "byte_pattern.h"
#include "log.h"
#include "memory.h"

namespace {

bool patchMatches(const CodePatch& patch) {
    std::vector<uint8_t> actual(patch.expected.size());
    if (!readMemory(patch.address, actual.data(), actual.size())) {
        logger::write("code check: %s at 0x%X unreadable", patch.name, static_cast<unsigned>(patch.address));
        return false;
    }
    const auto bad = firstMismatch(actual.data(), patch.expected.data(), actual.size());
    if (!bad) return true;
    logger::write("code check: %s at 0x%X differs at byte %zu (found %02X, expected %02X)", patch.name,
                  static_cast<unsigned>(patch.address), *bad, actual[*bad], patch.expected[*bad]);
    return false;
}

bool writeBytes(const CodePatch& patch, const uint8_t* bytes, size_t size) {
    void* target = reinterpret_cast<void*>(patch.address);
    DWORD previous = 0;
    if (!VirtualProtect(target, size, PAGE_EXECUTE_READWRITE, &previous)) {
        logger::write("code patch: %s at 0x%X cannot be made writable", patch.name,
                      static_cast<unsigned>(patch.address));
        return false;
    }
    memcpy(target, bytes, size);
    VirtualProtect(target, size, previous, &previous);
    FlushInstructionCache(GetCurrentProcess(), target, size);
    return true;
}

}  // namespace

bool codeMatches(const std::vector<CodePatch>& patches) {
    bool ok = true;
    for (const CodePatch& patch : patches) ok = patchMatches(patch) && ok;
    return ok;
}

bool applyCodePatches(const std::vector<CodePatch>& patches) {
    if (!codeMatches(patches)) return false;
    for (size_t written = 0; written < patches.size(); ++written) {
        const CodePatch& patch = patches[written];
        if (patch.replacement.empty() || writeBytes(patch, patch.replacement.data(), patch.replacement.size())) continue;
        for (size_t undone = 0; undone < written; ++undone) {  // all or nothing: put back what was written
            const CodePatch& earlier = patches[undone];
            if (!earlier.replacement.empty()) writeBytes(earlier, earlier.expected.data(), earlier.replacement.size());
        }
        return false;
    }
    return true;
}
