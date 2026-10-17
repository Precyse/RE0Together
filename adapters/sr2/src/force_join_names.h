#pragma once
#include <cctype>
#include <string>

// The tags in a session name (pure, unit tested). The browser tag is `cfv<VVV>-<digest>` (trailer_rules::tagFor: VVV the
// protocol version in three digits, the digest of every field the version gate compares); a force join session adds the
// launcher's token, `cfv<VVV>-<digest>-<token>`. The host adds its tag to its session name, the guest searches the browser
// for it, and a tag that holds the browser tag is listed by a browser filter of just that.
namespace force_join_names {

inline constexpr char kTagPrefix[] = "cfv";
inline constexpr char kTokenSeparator = '-';
inline constexpr int kVersionDigits = 3;

// The tag of a force join session: the browser tag, a dash and the launcher's token.
inline std::string sessionTag(const std::string& browserTag, const std::string& token) {
    return browserTag + kTokenSeparator + token;
}

// True when `name` holds `tag` that is not the start of a longer number. A longer tag that starts with it, behind a dash,
// holds it too (the browser tag is in every session tag of that build).
inline bool containsTag(const std::string& name, const std::string& tag) {
    for (size_t at = name.find(tag); at != std::string::npos; at = name.find(tag, at + 1)) {
        const size_t end = at + tag.size();
        if (end == name.size() || !std::isdigit(static_cast<unsigned char>(name[end]))) return true;
    }
    return false;
}

// True when `word` is a tag of any session: `cfv<digits>-` and then hex digits in one or more dash separated groups
// (`cfv003-1A2B3C4D`, `cfv003-1A2B3C4D-109775241`, the earlier `cfv002-109775241`).
inline bool isTag(const std::string& word) {
    const size_t prefix = sizeof(kTagPrefix) - 1;
    if (word.compare(0, prefix, kTagPrefix) != 0) return false;
    const size_t separator = word.find_first_not_of("0123456789", prefix);
    if (separator == std::string::npos || separator == prefix || word[separator] != kTokenSeparator) return false;
    bool groupHasDigit = false;
    for (size_t at = separator + 1; at < word.size(); ++at) {
        if (word[at] == kTokenSeparator) {
            if (!groupHasDigit) return false;
            groupHasDigit = false;
        } else if (std::isxdigit(static_cast<unsigned char>(word[at]))) {
            groupHasDigit = true;
        } else {
            return false;
        }
    }
    return groupHasDigit;
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

// `name` with `tag` appended, for a name buffer of `capacity` bytes (its end mark included): the name is cut to make room,
// the tag never is. A name that has the tag is returned as it is.
inline std::string withTag(const std::string& name, const std::string& tag, size_t capacity) {
    if (containsTag(name, tag)) return name;
    std::string base = withoutTags(name);
    const size_t room = capacity - 1 - tag.size() - 1;  // the end mark, then the space before the tag
    if (base.size() > room) base.resize(room);
    base = withoutTags(base);
    return base.empty() ? tag : base + " " + tag;
}

}  // namespace force_join_names
