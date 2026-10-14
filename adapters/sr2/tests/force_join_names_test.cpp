// The session name token (no game): the tag, the match rule and the host's name edit.
#include <cstdio>
#include <string>

#include "../src/force_join_names.h"

namespace {

constexpr size_t kCapacity = 0x40;
const std::string kToken = "109775241012345678";

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

}  // namespace

int main() {
    using namespace force_join_names;
    check(tagOf("123") == "cf123", "the tag is cf and the token");
    check(containsTag("Host cf123", "123") && containsTag("[DLC] Host cf123 x", "123") && containsTag("cf123", "123"),
          "a name with the tag matches, anywhere in it");
    check(!containsTag("Host", "123") && !containsTag("Host cf124", "123") && !containsTag("", "123"),
          "a name without the tag does not match");
    check(!containsTag("Host cf1234", "123") && containsTag("cf1234 cf123", "123"),
          "the tag must not be the start of a longer number");

    check(withTag("Host", "123", kCapacity) == "Host cf123", "the tag is appended after a space");
    check(withTag("", "123", kCapacity) == "cf123", "an empty name becomes the tag");
    check(withTag("[DLC] [Host] cf123", "123", kCapacity) == "[DLC] [Host] cf123", "a name that has the tag is unchanged");
    check(withTag("Host cf111", "123", kCapacity) == "Host cf123", "the tag of an earlier session is replaced");
    check(withTag("Host cf111 cf222  ", "123", kCapacity) == "Host cf123", "several earlier tags and spaces are dropped");
    check(withTag("cf111", "123", kCapacity) == "cf123", "a name that is only an earlier tag becomes the new tag");

    const std::string longName(100, 'x');
    const std::string cut = withTag(longName, kToken, kCapacity);
    check(cut.size() == kCapacity - 1 && containsTag(cut, kToken) && cut.compare(0, 4, "xxxx") == 0,
          "a long name is cut to 63 bytes and the tag stays whole");
    check(withTag(std::string(40, 'x') + " " + std::string(40, 'y'), kToken, kCapacity).size() < kCapacity,
          "a cut inside a word never leaves the buffer too small for its end mark");
    const std::string spaced = withTag(std::string(40, 'x') + std::string(30, ' ') + "tail", kToken, kCapacity);
    check(spaced.find("  ") == std::string::npos && containsTag(spaced, kToken),
          "spaces at the cut are not kept before the tag");
    return g_failures == 0 ? 0 : 1;
}
