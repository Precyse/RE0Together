// The tags in a session name (no game): the session tag, the match rule and the host's name edit.
#include <cstdio>
#include <string>

#include "../src/force_join_names.h"
#include "check.h"

namespace {

constexpr size_t kCapacity = 0x40;
const std::string kBrowserTag = "cfv003-1A2B3C4D";
const std::string kToken = "109775241012345678";

}  // namespace

int main() {
    using namespace force_join_names;
    const std::string tag = sessionTag(kBrowserTag, "123");
    check(tag == "cfv003-1A2B3C4D-123", "the session tag is the browser tag, a dash and the token");
    check(isTag("cfv003-1A2B3C4D") && isTag(tag) && isTag("cfv002-123") && isTag("cfv001-9") && !isTag("cfv002-") &&
              !isTag("cfv-123") && !isTag("cfv002") && !isTag("cf123") && !isTag("cfv002-12x") && !isTag("xcfv002-123") &&
              !isTag("") && !isTag("cfv003-1A2B3C4D-") && !isTag("cfv003--123") && !isTag("cfv003-1A2B-xyz"),
          "a tag is cfv, digits, a dash and groups of hex digits joined by dashes");
    check(containsTag("Host cfv003-1A2B3C4D-123", tag) && containsTag("[DLC] Host cfv003-1A2B3C4D-123 x", tag) &&
              containsTag("cfv003-1A2B3C4D-123", tag),
          "a name with the session tag matches, anywhere in it");
    check(!containsTag("Host", tag) && !containsTag("Host cfv003-1A2B3C4D-124", tag) && !containsTag("", tag) &&
              !containsTag("Host cfv003-FFFFFFFF-123", tag) && !containsTag("Host cfv002-123", tag),
          "a name without the tag, or with another digest or version, does not match");
    check(!containsTag("Host cfv003-1A2B3C4D-1234", tag) && containsTag("cfv003-1A2B3C4D-1234 cfv003-1A2B3C4D-123", tag),
          "the tag must not be the start of a longer number");
    check(containsTag("Host cfv003-1A2B3C4D", kBrowserTag) && containsTag("Host cfv003-1A2B3C4D-123", kBrowserTag) &&
              !containsTag("Host cfv003-1A2B3C4E-123", kBrowserTag),
          "the browser tag is in the browser tag and in every session tag of that build");

    check(withTag("Host", kBrowserTag, kCapacity) == "Host cfv003-1A2B3C4D", "the tag is appended after a space");
    check(withTag("", tag, kCapacity) == "cfv003-1A2B3C4D-123", "an empty name becomes the tag");
    check(withTag("[DLC] [Host] cfv003-1A2B3C4D-123", tag, kCapacity) == "[DLC] [Host] cfv003-1A2B3C4D-123",
          "a name that has the tag is unchanged");
    check(withTag("Host cfv003-1A2B3C4D-123", kBrowserTag, kCapacity) == "Host cfv003-1A2B3C4D-123",
          "a session tag counts as the browser tag");
    check(withTag("Host cfv003-1A2B3C4D", tag, kCapacity) == "Host cfv003-1A2B3C4D-123", "the browser tag becomes the session tag");
    check(withTag("Host cfv003-FFFFFFFF-111", tag, kCapacity) == "Host cfv003-1A2B3C4D-123", "the tag of another build is replaced");
    check(withTag("Host cfv002-111 cfv003-0000AAAA-222  ", tag, kCapacity) == "Host cfv003-1A2B3C4D-123",
          "several earlier tags and spaces are dropped");
    check(withTag("cfv002-111", tag, kCapacity) == "cfv003-1A2B3C4D-123", "a name that is only an earlier tag becomes the new tag");

    const std::string longTag = sessionTag(kBrowserTag, kToken);
    const std::string longName(100, 'x');
    const std::string cut = withTag(longName, longTag, kCapacity);
    check(cut.size() == kCapacity - 1 && containsTag(cut, longTag) && cut.compare(0, 4, "xxxx") == 0,
          "a long name is cut to 63 bytes and the tag stays whole");
    check(withTag(std::string(40, 'x') + " " + std::string(40, 'y'), longTag, kCapacity).size() < kCapacity,
          "a cut inside a word never leaves the buffer too small for its end mark");
    const std::string spaced = withTag(std::string(40, 'x') + std::string(30, ' ') + "tail", longTag, kCapacity);
    check(spaced.find("  ") == std::string::npos && containsTag(spaced, longTag), "spaces at the cut are not kept before the tag");
    return g_failures == 0 ? 0 : 1;
}
