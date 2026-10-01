#include "pattern_scan.h"

#include <windows.h>

#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

constexpr int kWildcard = -1;
constexpr int kHexBase = 16;

std::vector<int> parse(const char* pattern) {
    std::vector<int> bytes;
    for (const char* p = pattern; *p;) {
        if (*p == ' ') {
            ++p;
        } else if (*p == '?') {
            bytes.push_back(kWildcard);
            while (*p == '?') ++p;
        } else {
            char* end = nullptr;
            bytes.push_back(static_cast<int>(std::strtoul(p, &end, kHexBase)));
            p = end;
        }
    }
    return bytes;
}

bool textSection(uintptr_t& begin, uintptr_t& end) {
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        if (std::memcmp(section->Name, ".text", sizeof(".text")) == 0) {
            begin = base + section->VirtualAddress;
            end = begin + section->Misc.VirtualSize;
            return true;
        }
    }
    return false;
}

}  // namespace

namespace pattern_scan {

uintptr_t find(const char* pattern) {
    const std::vector<int> bytes = parse(pattern);
    uintptr_t begin = 0, end = 0;
    if (bytes.empty() || !textSection(begin, end)) return 0;
    const auto* code = reinterpret_cast<const uint8_t*>(begin);
    const size_t size = end - begin;
    for (size_t i = 0; i + bytes.size() <= size; ++i) {
        size_t k = 0;
        while (k < bytes.size() && (bytes[k] == kWildcard || code[i + k] == bytes[k])) ++k;
        if (k == bytes.size()) return begin + i;
    }
    return 0;
}

uintptr_t ripTarget(uintptr_t match, int dispOffset, int instructionEnd) {
    int32_t disp = 0;
    std::memcpy(&disp, reinterpret_cast<const void*>(match + dispOffset), sizeof(disp));
    return match + instructionEnd + disp;
}

}  // namespace pattern_scan
