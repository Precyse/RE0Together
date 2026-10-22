#pragma once
#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>

// The pure rules of the player name the launcher hands the game (milestone 12, unit tested; the launcher's PlayerName.cs
// shapes the name, this is the adapter's boundary check). The name is UTF-8 in the join file and UTF-16 in the game.
namespace player_name_rules {

constexpr size_t kMaxChars = 15;         // the gamertag length the console UI was laid out for
constexpr uint32_t kBufferChars = 0x40;  // the game's name buffers hold 0x40 wide characters, terminator included

constexpr char32_t kLastAscii = 0x7E;
constexpr char32_t kFirstLatin1 = 0xA1;
constexpr char32_t kLastLatin1 = 0xFF;
constexpr char32_t kFirstPrintable = 0x20;

constexpr uint8_t kLeadLatin1Low = 0xC2;   // the lead bytes of U+0080..U+00FF
constexpr uint8_t kLeadLatin1High = 0xC3;
constexpr uint8_t kContinuationMask = 0xC0;
constexpr uint8_t kContinuationTag = 0x80;
constexpr uint8_t kContinuationBits = 0x3F;
constexpr uint8_t kLeadBits = 0x1F;
constexpr unsigned kContinuationShift = 6;
constexpr uint8_t kAsciiLimit = 0x80;

// The characters the launcher sends: printable ASCII and the printable Latin-1 letters and signs.
inline bool allowed(char32_t codePoint) {
    return (codePoint >= kFirstPrintable && codePoint <= kLastAscii) || (codePoint >= kFirstLatin1 && codePoint <= kLastLatin1);
}

// The name as UTF-16, or none when it is empty, longer than kMaxChars, malformed, or holds a character outside `allowed`.
// Every allowed character is one byte of ASCII or a two byte sequence led by C2 or C3, so no other form can pass.
inline std::optional<std::u16string> parse(const std::string& utf8) {
    std::u16string name;
    for (size_t i = 0; i < utf8.size(); ++i) {
        const uint8_t lead = static_cast<uint8_t>(utf8[i]);
        char32_t codePoint = lead;
        if (lead >= kAsciiLimit) {
            if (lead != kLeadLatin1Low && lead != kLeadLatin1High) return std::nullopt;
            if (++i == utf8.size()) return std::nullopt;
            const uint8_t next = static_cast<uint8_t>(utf8[i]);
            if ((next & kContinuationMask) != kContinuationTag) return std::nullopt;
            codePoint = static_cast<char32_t>(((lead & kLeadBits) << kContinuationShift) | (next & kContinuationBits));
        }
        if (!allowed(codePoint) || name.size() == kMaxChars) return std::nullopt;
        name.push_back(static_cast<char16_t>(codePoint));
    }
    if (name.empty()) return std::nullopt;
    return name;
}

// How many characters of a name fit a buffer of `cap` wide characters (the game limits the cap to its buffer size), the
// terminator included.
inline size_t charsThatFit(uint32_t cap) {
    const uint32_t room = std::min(cap, kBufferChars);
    return room == 0 ? 0 : room - 1;
}

// The name cut to what fits `cap`.
inline std::u16string fit(const std::u16string& name, uint32_t cap) { return name.substr(0, charsThatFit(cap)); }

}  // namespace player_name_rules
