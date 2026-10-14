// The pure rules of milestone 5d (no game): resend detection by real source and the local address list order.
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
    using home_network_rules::putFirst;
    using home_network_rules::sameRealSource;

    check(sameRealSource(kPublicIp, kPortOfSecondPlayer, kPublicIp, kPortOfSecondPlayer), "same IP and port: a resend");
    check(!sameRealSource(kPublicIp, kPortOfSecondPlayer, kPublicIp, kPortOfThirdPlayer),
          "same IP, another port (two machines behind one NAT): not a resend");
    check(!sameRealSource(kPublicIp, kPortOfSecondPlayer, kPublicIp + 1, kPortOfSecondPlayer), "another IP: not a resend");

    uint8_t virtualAdapter[4] = {192, 168, 56, 1};
    uint8_t lan[4] = {192, 168, 1, 20};
    uint8_t other[4] = {10, 0, 0, 5};
    char* list[] = {reinterpret_cast<char*>(virtualAdapter), reinterpret_cast<char*>(lan), nullptr};
    putFirst(list, lan);
    check(list[0] == reinterpret_cast<char*>(lan) && list[1] == reinterpret_cast<char*>(virtualAdapter) && !list[2],
          "an address in the list moves to the front and the first one takes its place");

    char* single[] = {reinterpret_cast<char*>(virtualAdapter), nullptr};
    putFirst(single, other);
    check(virtualAdapter[0] == 10 && virtualAdapter[3] == 5 && single[0] == reinterpret_cast<char*>(virtualAdapter),
          "an address that is not in the list overwrites the first entry");

    char* empty[] = {nullptr};
    putFirst(empty, other);
    putFirst(nullptr, other);
    check(!empty[0], "an empty or missing list stays as it is");
    return g_failures == 0 ? 0 : 1;
}
