#include "decima/localized_text.h"

#include <array>

#include "decima/safe_read.h"

namespace {

constexpr uintptr_t kTextChars = 0x20, kTextLength = 0x28;  // LocalizedTextResource: UTF-8 text and its length
constexpr size_t kMaxTextBytes = 63;

}  // namespace

namespace decima {

std::string localizedText(uintptr_t resource) {
    const uintptr_t chars = resource ? readPointer(resource + kTextChars) : 0;
    uint32_t length = 0;
    if (!chars || !safeRead(resource + kTextLength, length)) return {};
    std::array<char, kMaxTextBytes> buffer{};
    const size_t size = length < buffer.size() ? length : buffer.size();
    return safeCopy(buffer.data(), chars, size) ? std::string(buffer.data(), size) : std::string{};
}

}  // namespace decima
