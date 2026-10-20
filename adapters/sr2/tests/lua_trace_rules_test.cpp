// The milestone 16 wave 2 Lua trace (no game): the coop line rules.
#include <cstdio>
#include <string>

#include "../src/lua_trace_rules.h"
#include "check.h"

namespace {

using namespace lua_trace_rules;

constexpr size_t kLongTextLength = 300;

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

}  // namespace

int main() {
    theCoopLine();
    theRateKey();
    return g_failures == 0 ? 0 : 1;
}
