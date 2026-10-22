#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "fnv1a.h"

// The content hash of the version gate (pure, unit tested): FNV-1a 64 over a documented byte stream, so two machines with
// the same dinput8.dll and the same coop\scripts folder get the same value (docs/SR2_NOTES.md, "Version gate").
//
//   1. dinput8.dll:  u32 LE size, then the bytes
//   2. every file of coop\scripts, in ascending byte order of its lower case ASCII name (script_override's names):
//      the name, 0x00, u32 LE size, then the bytes
//
// A file that cannot be read contributes the size kUnreadableSize and no bytes.
namespace content_hash_rules {

constexpr uint32_t kUnreadableSize = 0xFFFFFFFF;
constexpr size_t kSizeBytes = 4;

// A file's bytes, or none when it cannot be read.
using File = std::optional<std::vector<uint8_t>>;
using ReadScript = std::function<File(const std::string& name)>;

struct Result {
    uint64_t hash = fnv1a::kOffsetBasis;
    size_t dllBytes = 0;                  // 0 when dinput8.dll could not be read
    size_t scripts = 0;
    std::vector<std::string> unreadable;  // "dinput8.dll" and the scripts that could not be read
};

namespace detail {

inline uint64_t addSize(uint64_t state, uint32_t size) {
    uint8_t bytes[kSizeBytes];
    for (size_t i = 0; i < kSizeBytes; ++i) bytes[i] = static_cast<uint8_t>(size >> (8 * i));
    return fnv1a::hash64(bytes, sizeof(bytes), state);
}

// The size, then the bytes of `file`; the file is named in `unreadable` when it has none.
inline uint64_t addFile(uint64_t state, const File& file, const std::string& name, Result& result) {
    if (!file) {
        result.unreadable.push_back(name);
        return addSize(state, kUnreadableSize);
    }
    state = addSize(state, static_cast<uint32_t>(file->size()));
    return fnv1a::hash64(file->data(), file->size(), state);
}

}  // namespace detail

inline constexpr char kDllName[] = "dinput8.dll";

// `names` are the lower case names of the files in coop\scripts, `readScript` reads one of them.
inline Result compute(const File& dll, const std::set<std::string>& names, const ReadScript& readScript) {
    Result result;
    result.dllBytes = dll ? dll->size() : 0;
    result.hash = detail::addFile(result.hash, dll, kDllName, result);
    for (const std::string& name : names) {  // a std::set walks in ascending byte order
        const uint8_t end = 0;
        result.hash = fnv1a::hash64(reinterpret_cast<const uint8_t*>(name.data()), name.size(), result.hash);
        result.hash = fnv1a::hash64(&end, 1, result.hash);
        result.hash = detail::addFile(result.hash, readScript(name), name, result);
        ++result.scripts;
    }
    return result;
}

}  // namespace content_hash_rules
