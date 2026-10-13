// Config parsing and the max_players clamp (no game).
#include <cstdio>

#include "../src/config.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

}  // namespace

int main() {
    check(parseConfig("").maxPlayers == 4 && parseConfig("").trace, "defaults: 4 players, trace on");
    check(parseConfig("max_players=3\n").maxPlayers == 3, "max_players read");
    check(parseConfig(" max_players = 2 \r\ntrace=0\r\n").maxPlayers == 2, "spaces and CRLF trimmed");
    check(!parseConfig("trace=0\n").trace, "trace off");
    check(parseConfig("max_players=1\n").maxPlayers == 2, "below 2 clamps to 2");
    check(parseConfig("max_players=99\n").maxPlayers == 12, "above 12 clamps to 12");
    check(parseConfig("max_players=abc\ntrace=x\n").maxPlayers == 4, "bad values keep the defaults");
    check(parseConfig("max_players=\nunknown=5\nnoequals\n").maxPlayers == 4, "empty value and unknown keys ignored");
    return g_failures == 0 ? 0 : 1;
}
