// Finding the host in the browser's list (no game): the index counts only the listed entries, as the game's copy does.
#include <cstdio>
#include <string>

#include "../src/browser_match.h"

namespace {

const std::string kTag = "cfv003-1A2B3C4D-555";

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

}  // namespace

int main() {
    using browser_match::Entry;
    const std::vector<Entry> list = {
        {0x1000, true, "Other"},
        {0x2000, false, "Hidden cfv003-1A2B3C4D-555"},  // not listed: never counted, never joined
        {0x3000, true, "Mine cfv003-1A2B3C4D-555"},
        {0x4000, true, "Mine cfv003-1A2B3C4D-555"},
    };
    check(browser_match::listedCount(list) == 3, "three of the four entries are listed");
    const auto match = browser_match::find(list, kTag);
    check(match && match->index == 1 && match->address == 0x3000,
          "the host is the second listed entry; the unlisted one before it is not counted");
    check(!browser_match::find(list, "cfv003-1A2B3C4D-556"), "no entry has another token");
    check(!browser_match::find({}, kTag), "an empty list has no match");
    check(!browser_match::find({{0x1000, true, "Mine cfv003-1A2B3C4D-5551"}}, kTag), "a longer number is not the token");
    check(browser_match::find({{0x1000, true, "cfv003-1A2B3C4D-555"}}, kTag)->index == 0, "the first listed entry is index 0");
    check(!browser_match::find({{0x1000, true, "Mine cfv003-FFFFFFFF-555"}}, kTag),
          "a host of another build (another digest) with the same token is not found");
    return g_failures == 0 ? 0 : 1;
}
