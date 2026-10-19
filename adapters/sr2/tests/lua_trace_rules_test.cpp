// The milestone 16 wave 2 Lua trace (no game): the coop line rules and the LT1 site.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

#include "../src/lua_trace_rules.h"
#include "../src/lua_trace_sites.h"
#include "../src/sr2_addresses.h"
#include "juiced_targets.h"
#include "other_sites.h"

namespace {

using namespace lua_trace_rules;

constexpr uintptr_t kJuicedMargin = 16;
constexpr size_t kLt1Bytes = 11;
constexpr size_t kLongTextLength = 300;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

std::string line(const std::string& text) { return coopLine(text.c_str(), text.size()); }

void theCoopLine() {
    check(line("coop: hud slot #PLAYER3# 2") == "hud slot #PLAYER3# 2", "the body after the prefix");
    check(line("coop: ") == "", "the prefix alone has an empty body");
    check(line("hud slot #PLAYER3# 2") == "", "a text without the prefix is not a coop line");
    check(line("coop:") == "" && line("") == "", "a text shorter than the prefix is not a coop line");
    check(line("Coop: x") == "" && line(" coop: x") == "", "the prefix must match exactly, from the first character");
    check(line("coop: a\tb\nc\x7f") == "a?b?c?", "control characters and DEL show as '?'");

    const std::string longText = std::string(kCoopPrefix) + std::string(kLongTextLength, 'x');
    check(line(longText) == std::string(kLineLimit, 'x'), "a long body is clipped at 200 characters");

    const char withNul[] = "coop: abc\0def";
    check(coopLine(withNul, sizeof(withNul) - 1) == "abc", "the body ends at the first NUL");
    check(coopLine(withNul, 3) == "", "only the readable size counts");
}

void theRateKey() {
    const std::string longLine(kLongTextLength, 'y');
    check(rateKey("seat boat #PLAYER3# nil") == "seat boat #PLAYER3# nil", "a short line is its own key");
    check(rateKey(longLine) == std::string(kRateKeyLength, 'y'), "the key is the first 64 characters");
    check(rateKey(longLine + "a") == rateKey(longLine + "b"), "lines that differ only after 64 characters share a key");
    check(rateKey("seat boat #PLAYER3# 5") != rateKey("seat boat #PLAYER4# 4"), "lines for different players have different keys");
}

void theSite() {
    const partner_sites::Site& site = lua_trace_sites::kSites[0];
    const auto bytes = partner_sites::parseHex(site.expected);
    check(std::size(lua_trace_sites::kSites) == 1 && std::strcmp(site.id, "LT1") == 0, "one site, LT1");
    check(site.address == sr2::kLuaMissionDebug, "LT1 is at mission_debug 0xA398C0");
    check(site.kind == partner_sites::Kind::HookEntry && site.gate == partner_sites::Gate::Now && site.tail.empty(), "a hook, written when its set is");
    check(site.thunk == partner_sites::Thunk::TraceLuaDebug, "its thunk is the Lua trace");
    check(bytes.size() == kLt1Bytes && bytes[0] == 0x55 && bytes[1] == 0x8B && bytes[2] == 0xEC, "its 11 bytes parse and start with push ebp; mov ebp, esp");

    const uintptr_t first = site.address - kJuicedMargin;
    const uintptr_t last = site.address + bytes.size() + kJuicedMargin;
    check(std::none_of(std::begin(juiced::kTargets), std::end(juiced::kTargets), [&](uint32_t target) { return target >= first && target < last; }),
          "no Juiced target is within 16 bytes of LT1");

    std::vector<other_sites::Range> others = other_sites::milestones3to8();
    for (const auto& range : other_sites::activities()) others.push_back(range);
    for (const auto& range : other_sites::players3and4()) others.push_back(range);
    check(!other_sites::anyOverlap(others, site.address, bytes.size()), "LT1 overlaps no site of milestones 3 to 9");
}

}  // namespace

int main() {
    theCoopLine();
    theRateKey();
    theSite();
    return g_failures == 0 ? 0 : 1;
}
