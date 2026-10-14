// The pure rule of milestone 5d (no game): resend detection by real source.
#include <cstdio>

#include "../src/home_network_rules.h"

namespace {

constexpr uint32_t kPublicIp = 0x0100007F;
constexpr uint16_t kPortOfSecondPlayer = 50001;
constexpr uint16_t kPortOfThirdPlayer = 50002;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

}  // namespace

int main() {
    using home_network_rules::sameRealSource;

    check(sameRealSource(kPublicIp, kPortOfSecondPlayer, kPublicIp, kPortOfSecondPlayer), "same IP and port: a resend");
    check(!sameRealSource(kPublicIp, kPortOfSecondPlayer, kPublicIp, kPortOfThirdPlayer),
          "same IP, another port (two machines behind one NAT): not a resend");
    check(!sameRealSource(kPublicIp, kPortOfSecondPlayer, kPublicIp + 1, kPortOfSecondPlayer), "another IP: not a resend");
    return g_failures == 0 ? 0 : 1;
}
