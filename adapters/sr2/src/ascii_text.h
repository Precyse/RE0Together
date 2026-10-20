#pragma once
#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

// ASCII text helpers shared by the file name and adapter name rules.
namespace ascii_text {

constexpr unsigned kAsciiLimit = 0x80;

// Wide text as ASCII; anything else becomes '?'. A null pointer is empty text.
inline std::string narrow(const wchar_t* text) {
    std::string out;
    for (; text && *text; ++text) out += static_cast<unsigned>(*text) < kAsciiLimit ? static_cast<char>(*text) : '?';
    return out;
}

// `text` with every ASCII letter in lower case.
inline std::string lowered(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

}  // namespace ascii_text
