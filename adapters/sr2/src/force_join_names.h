#pragma once
#include <cctype>
#include <cstdio>
#include <string>

#include "version.h"

// The token as it appears in a session name (pure, unit tested): `cfv<VVV>-<token>`, VVV the protocol version in three
// digits (so cfv002 never matches cfv0020). The host adds it to its session name, the guest searches the browser for it.
namespace force_join_names {

inline constexpr char kTagPrefix[] = "cfv";
inline constexpr char kTokenSeparator = '-';
inline constexpr int kVersionDigits = 3;

inline std::string tagOf(const std::string& token) {
    char version[kVersionDigits + 1];
    snprintf(version, sizeof(version), "%0*u", kVersionDigits, static_cast<unsigned>(version::kProtocolVersion));
    return kTagPrefix + std::string(version) + kTokenSeparator + token;
}

// True when `name` holds the tag of `token` that is not the start of a longer number.
inline bool containsTag(const std::string& name, const std::string& token) {
    const std::string tag = tagOf(token);
    for (size_t at = name.find(tag); at != std::string::npos; at = name.find(tag, at + 1)) {
        const size_t end = at + tag.size();
        if (end == name.size() || !std::isdigit(static_cast<unsigned char>(name[end]))) return true;
    }
    return false;
}

// True when `word` is a tag of any session: `cfv<digits>-<digits>`.
inline bool isTag(const std::string& word) {
    const size_t prefix = sizeof(kTagPrefix) - 1;
    if (word.compare(0, prefix, kTagPrefix) != 0) return false;
    const size_t separator = word.find_first_not_of("0123456789", prefix);
    if (separator == std::string::npos || separator == prefix || word[separator] != kTokenSeparator) return false;
    return separator + 1 < word.size() && word.find_first_not_of("0123456789", separator + 1) == std::string::npos;
}

// `name` without the trailing tags of earlier sessions, and without trailing spaces.
inline std::string withoutTags(std::string name) {
    for (;;) {
        const size_t end = name.find_last_not_of(' ');
        name.resize(end == std::string::npos ? 0 : end + 1);
        const size_t space = name.find_last_of(' ');
        const size_t start = space == std::string::npos ? 0 : space + 1;
        const std::string word = name.substr(start);
        if (!isTag(word)) return name;
        name.resize(start);
    }
}

// `name` with the tag of `token` appended, for a name buffer of `capacity` bytes (its end mark included): the name is
// cut to make room, the tag never is. A name that has the tag is returned as it is.
inline std::string withTag(const std::string& name, const std::string& token, size_t capacity) {
    if (containsTag(name, token)) return name;
    const std::string tag = tagOf(token);
    std::string base = withoutTags(name);
    const size_t room = capacity - 1 - tag.size() - 1;  // the end mark, then the space before the tag
    if (base.size() > room) base.resize(room);
    base = withoutTags(base);
    return base.empty() ? tag : base + " " + tag;
}

}  // namespace force_join_names
