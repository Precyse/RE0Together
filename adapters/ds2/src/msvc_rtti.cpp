#include "msvc_rtti.h"

#include <windows.h>

#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

namespace {

constexpr uint32_t kColSignatureX64 = 1;
constexpr size_t kTypeDescriptorName = 0x10;

// x64 complete object locator; RVAs are relative to the module base.
struct CompleteObjectLocator {
    uint32_t signature;
    uint32_t offset;
    uint32_t constructorDisplacement;
    uint32_t typeDescriptor;
    uint32_t classHierarchy;
    uint32_t self;
};

struct Range {
    uintptr_t begin = 0, end = 0;
};

Range sectionOf(uintptr_t base, const char* name) {
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
    const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        if (std::strncmp(reinterpret_cast<const char*>(section->Name), name, IMAGE_SIZEOF_SHORT_NAME) == 0) {
            return {base + section->VirtualAddress, base + section->VirtualAddress + section->Misc.VirtualSize};
        }
    }
    return {};
}

uintptr_t find(Range range, const void* needle, size_t size, size_t step, uintptr_t from) {
    for (uintptr_t p = from ? from : range.begin; p + size <= range.end; p += step) {
        if (std::memcmp(reinterpret_cast<const void*>(p), needle, size) == 0) return p;
    }
    return 0;
}

uintptr_t scanForVtable(const char* className) {
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const Range data = sectionOf(base, ".data"), rdata = sectionOf(base, ".rdata");
    const std::string decorated = std::string(".?AV") + className + "@@";
    const uintptr_t name = find(data, decorated.c_str(), decorated.size() + 1, 1, 0);
    if (!name) return 0;
    const auto descriptorRva = static_cast<uint32_t>(name - kTypeDescriptorName - base);
    for (uintptr_t p = find(rdata, &descriptorRva, sizeof(descriptorRva), sizeof(uint32_t), 0); p;
         p = find(rdata, &descriptorRva, sizeof(descriptorRva), sizeof(uint32_t), p + sizeof(uint32_t))) {
        const auto* col = reinterpret_cast<const CompleteObjectLocator*>(p - offsetof(CompleteObjectLocator, typeDescriptor));
        const auto colAddress = reinterpret_cast<uintptr_t>(col);
        if (col->signature != kColSignatureX64 || col->self != colAddress - base || col->offset != 0) continue;
        const uintptr_t slot = find(rdata, &colAddress, sizeof(colAddress), sizeof(uintptr_t), 0);
        if (slot) return slot + sizeof(uintptr_t);
    }
    return 0;
}

std::mutex g_mutex;  // guards g_found; held across a scan, so two threads never scan for the same name
std::unordered_map<std::string, uintptr_t> g_found;  // class name -> vtable; a name not found is looked for again next time

}  // namespace

namespace msvc_rtti {

uintptr_t vtableOf(const char* className) {
    std::lock_guard lock(g_mutex);
    if (const auto known = g_found.find(className); known != g_found.end()) return known->second;
    const uintptr_t vtable = scanForVtable(className);
    if (vtable) g_found.emplace(className, vtable);
    return vtable;
}

}  // namespace msvc_rtti
