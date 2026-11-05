#include "troc_catalogue.h"

#include <windows.h>

#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include "log.h"
#include "paths.h"
#include "safe_read.h"

namespace {

// The static Troc instances (1,175 in build 0x6AEB7D59, 0x90 bytes apart) live in the image's writable data: their
// derived vtables are written there by dynamic initialisers. Their section's name changes with every build, so every
// writable, non-executable section is scanned rather than a fixed range.
constexpr uintptr_t kInstanceAlign = 8;

// Instance layout, unchanged across the 0x6AB28F00 and 0x6AEB7D59 builds (checked on both exes: every instance holds a
// unique opcode at +0x0C and min/max 5/32710 at +0x18/+0x1A): vtable +0x00, opcode +0x0C, min +0x18, max +0x1A; kind
// +0x08, channel +0x14, flags +0x16, deferred +0x1C, disabled +0x21 from the notes [N, STUDY.md 2.5].
constexpr uintptr_t kKind = 0x08, kOpcode = 0x0C, kChannel = 0x14, kFlags = 0x16, kMin = 0x18, kMax = 0x1A,
                    kDeferred = 0x1C, kDisabled = 0x21;
constexpr uint16_t kLargestPacket = 0x7FDA;  // OnRecvPacket refuses totals above this

// x64 MSVC RTTI: the complete object locator sits in the slot before the vtable; its RVAs are image relative.
constexpr uint32_t kColSignatureX64 = 1;
constexpr uintptr_t kColTypeDescriptorRva = 0x0C;
constexpr uintptr_t kTypeDescriptorName = 0x10;
constexpr size_t kLongestTypeName = 128;

troc_table::Table g_table;

uintptr_t imageBase() { return reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)); }

uint32_t imageSize() {
    const uintptr_t base = imageBase();
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    return reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew)->OptionalHeader.SizeOfImage;
}

bool inImage(uintptr_t address) { return address >= imageBase() && address < imageBase() + imageSize(); }

struct Range {
    uintptr_t begin, end;
};

std::vector<Range> writableData() {
    std::vector<Range> out;
    const uintptr_t base = imageBase();
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
    const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        const DWORD flags = section->Characteristics;
        if ((flags & IMAGE_SCN_MEM_WRITE) && !(flags & IMAGE_SCN_MEM_EXECUTE)) {
            out.push_back({base + section->VirtualAddress, base + section->VirtualAddress + section->Misc.VirtualSize});
        }
    }
    return out;
}

// The RTTI type name of the object whose vtable is `vtable`, or empty.
std::string typeName(uintptr_t vtable) {
    if (!inImage(vtable)) return {};
    const uintptr_t col = safe_read::pointer(vtable - sizeof(uintptr_t));
    uint32_t signature = 0, descriptorRva = 0;
    if (!inImage(col) || !safe_read::value(col, signature) || signature != kColSignatureX64) return {};
    if (!safe_read::value(col + kColTypeDescriptorRva, descriptorRva)) return {};
    char name[kLongestTypeName] = {};
    if (!safe_read::bytes(imageBase() + descriptorRva + kTypeDescriptorName, name, sizeof(name) - 1)) return {};
    return std::string(name, strnlen(name, sizeof(name) - 1));
}

template <typename T>
T field(uintptr_t instance, uintptr_t offset) {
    T out{};
    safe_read::value(instance + offset, out);
    return out;
}

// The message an address holds, when it is a pa::TrocTr instance with sane limits.
std::optional<troc_table::Info> instanceAt(uintptr_t instance) {
    const std::string decorated = typeName(safe_read::pointer(instance));
    const std::string_view name = troc_table::messageName(decorated);
    if (name.empty()) return std::nullopt;
    troc_table::Info info;
    info.name = std::string(name);
    info.opcode = field<uint16_t>(instance, kOpcode);
    info.kind = field<uint8_t>(instance, kKind);
    info.channel = field<uint8_t>(instance, kChannel);
    info.flags = field<uint16_t>(instance, kFlags);
    info.minSize = field<uint16_t>(instance, kMin);
    info.maxSize = field<uint16_t>(instance, kMax);
    info.deferred = field<uint8_t>(instance, kDeferred);
    info.disabled = field<uint8_t>(instance, kDisabled);
    if (info.minSize > info.maxSize || info.maxSize > kLargestPacket) return std::nullopt;
    return info;
}

void writeTsv() {
    FILE* out = _wfopen((coopDirectory() + L"\\troc_catalogue.tsv").c_str(), L"w");
    if (!out) return;
    std::fprintf(out, "opcode\tname\tkind\tchannel\tflags\tmin\tmax\tdeferred\tdisabled\n");
    for (const auto& [opcode, info] : g_table.all()) {
        std::fprintf(out, "%u\t%s\t%u\t%u\t0x%X\t%u\t%u\t%u\t%u\n", opcode, info.name.c_str(), info.kind, info.channel,
                     info.flags, info.minSize, info.maxSize, info.deferred, info.disabled);
    }
    std::fclose(out);
}

}  // namespace

namespace troc_catalogue {

bool build() {
    unsigned conflicts = 0;
    for (const Range& range : writableData()) {
        for (uintptr_t instance = range.begin; instance + kInstanceAlign <= range.end; instance += kInstanceAlign) {
            const std::optional<troc_table::Info> info = instanceAt(instance);
            if (info && !g_table.add(*info)) ++conflicts;
        }
    }
    const auto missing = g_table.missing(kRequired);
    logger::write("troc_catalogue: %zu messages, %u duplicates, %zu required missing", g_table.size(), conflicts,
                  missing.size());
    for (std::string_view name : missing) logger::write("troc_catalogue: missing %.*s", static_cast<int>(name.size()), name.data());
    writeTsv();
    return missing.empty();
}

const troc_table::Table& table() { return g_table; }

const char* nameOf(uint16_t opcode) {
    const troc_table::Info* info = g_table.find(opcode);
    return info ? info->name.c_str() : "?";
}

}  // namespace troc_catalogue
