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
    check(parseConfig("max_players=99\n").maxPlayers == 8, "above 8 clamps to 8");
    check(parseConfig("max_players=abc\ntrace=x\n").maxPlayers == 4, "bad values keep the defaults");
    check(parseConfig("max_players=\nunknown=5\nnoequals\n").maxPlayers == 4, "empty value and unknown keys ignored");
    check(!parseConfig("").partnerExperimental, "partner_experimental defaults to off");
    check(parseConfig("partner_experimental=1\n").partnerExperimental, "partner_experimental on");
    check(!parseConfig("partner_experimental=0\nmax_players=3\n").partnerExperimental, "partner_experimental off");
    check(parseConfig("partner_experimental=1\nmax_players=3\ntrace=0\n").maxPlayers == 3, "partner_experimental leaves the other keys alone");
    check(parseConfig("").gamePort == 4200 && parseConfig("").rejectedGamePort == 0, "game_port defaults to 4200");
    check(parseConfig("game_port=4201\n").gamePort == 4201, "game_port read");
    check(parseConfig("game_port=1024\n").gamePort == 1024 && parseConfig("game_port=65535\n").gamePort == 65535,
          "game_port accepts 1024 and 65535");
    const Config low = parseConfig("game_port=1023\n");
    check(low.gamePort == 4200 && low.rejectedGamePort == 1023, "game_port below 1024 is rejected and the default kept");
    const Config high = parseConfig("game_port=65536\nmax_players=3\n");
    check(high.gamePort == 4200 && high.rejectedGamePort == 65536 && high.maxPlayers == 3, "game_port above 65535 is rejected");
    check(parseConfig("game_port=abc\n").gamePort == 4200, "a game_port that is not a number keeps the default");
    check(parseConfig("").transport == Transport::Native && parseConfig("").rejectedTransport.empty(),
          "transport defaults to native");
    check(parseConfig("transport=steam\n").transport == Transport::Steam, "transport=steam read");
    check(parseConfig("transport=native\n").transport == Transport::Native, "transport=native read");
    check(parseConfig(" transport = steam \r\nmax_players=3\n").transport == Transport::Steam, "spaces and CRLF trimmed");
    const Config odd = parseConfig("transport=tcp\nmax_players=3\n");
    check(odd.transport == Transport::Native && odd.rejectedTransport == "tcp" && odd.maxPlayers == 3,
          "an unknown transport keeps native and is reported");
    check(parseConfig("transport=\n").transport == Transport::Native && parseConfig("transport=\n").rejectedTransport.empty(),
          "an empty transport keeps native without a report");
    check(parseConfig("transport=1\n").transport == Transport::Native, "a number is not a transport");
    check(parseConfig("transport=steam\ntransport=bogus\n").transport == Transport::Steam, "a later bad value keeps the earlier good one");
    return g_failures == 0 ? 0 : 1;
}
