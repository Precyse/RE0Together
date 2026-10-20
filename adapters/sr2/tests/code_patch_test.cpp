// Code patches against a buffer in the test's own memory: all or nothing on apply, and the revert that site_installer uses when a hook of
// the set cannot be installed.
#include <windows.h>

#include <cstdint>
#include <initializer_list>
#include <utility>
#include <vector>

#include "../src/code_patch.h"
#include "check.h"

namespace logger {
void write(const char*, ...) {}
}  // namespace logger

namespace {

alignas(16) uint8_t g_code[16];

constexpr uint8_t kOriginal[] = {0x8B, 0x45, 0x08, 0x90, 0x90, 0xC3};

void resetCode() {
    for (uint8_t& byte : g_code) byte = 0;
    for (size_t i = 0; i < sizeof(kOriginal); ++i) g_code[i] = kOriginal[i];
}

CodePatch patchAt(size_t offset, std::vector<uint8_t> expected, std::vector<uint8_t> replacement) {
    return {"test", reinterpret_cast<uintptr_t>(g_code) + offset, std::move(expected), std::move(replacement)};
}

bool codeIs(std::initializer_list<uint8_t> bytes) {
    size_t i = 0;
    for (const uint8_t byte : bytes) {
        if (g_code[i++] != byte) return false;
    }
    return true;
}

void theApplyAndRevert() {
    resetCode();
    const std::vector<CodePatch> patches = {patchAt(0, {0x8B, 0x45, 0x08}, {0xE9, 0x01, 0x02}), patchAt(3, {0x90, 0x90}, {0xCC}), patchAt(5, {0xC3}, {})};
    check(applyCodePatches(patches), "matching patches are written");
    check(codeIs({0xE9, 0x01, 0x02, 0xCC, 0x90, 0xC3}), "every replacement is in place, a check-only patch writes nothing");
    revertCodePatches(patches);
    check(codeIs({0x8B, 0x45, 0x08, 0x90, 0x90, 0xC3}), "the revert puts the original bytes back over the replacements only");
}

void theMismatchWritesNothing() {
    resetCode();
    const std::vector<CodePatch> patches = {patchAt(0, {0x8B, 0x45, 0x08}, {0xE9, 0x01, 0x02}), patchAt(3, {0x91}, {0xCC})};
    check(!applyCodePatches(patches), "one differing patch fails the set");
    check(codeIs({0x8B, 0x45, 0x08, 0x90, 0x90, 0xC3}), "and nothing is written");
}

// An address that is no longer mapped: VirtualProtect refuses it, as it can refuse a page of the exe.
CodePatch unwritablePatch() {
    void* page = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    const uintptr_t address = reinterpret_cast<uintptr_t>(page);
    VirtualFree(page, 0, MEM_RELEASE);
    return {"gone", address, {0x90}, {0xCC}};
}

void theRevertReportsAWriteThatFailsAndGoesOn() {
    resetCode();
    const std::vector<CodePatch> written = {patchAt(0, {0x8B, 0x45, 0x08}, {0xE9, 0x01, 0x02})};
    check(applyCodePatches(written), "the first patch is written");
    const std::vector<CodePatch> patches = {unwritablePatch(), written[0]};
    check(!revertCodePatches(patches), "a revert with a write that fails says so");
    check(codeIs({0x8B, 0x45, 0x08, 0x90, 0x90, 0xC3}), "and still puts back the patches after the one that failed");
    check(revertCodePatches(written), "a revert that writes everything says so");
}

}  // namespace

int main() {
    theApplyAndRevert();
    theMismatchWritesNothing();
    theRevertReportsAWriteThatFailsAndGoesOn();
    return g_failures == 0 ? 0 : 1;
}
