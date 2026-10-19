// Checks the sender policies of the frame router (no game). Exit 0 when every check passes.
#include <cstdio>
#include <cstring>

#include "../src/frame_router_rule.h"

namespace {

namespace rule = frame_router_rule;
using rule::SenderPolicy;

constexpr uint8_t kHost = 0;
constexpr uint8_t kGuest = 1;
constexpr uint8_t kOtherGuest = 2;
int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

void testAny() {
    check(rule::admits(SenderPolicy::Any, kHost, kHost, true), "Any admits the host on the host");
    check(rule::admits(SenderPolicy::Any, kHost, kHost, false), "Any admits the host on a guest");
    check(rule::admits(SenderPolicy::Any, kGuest, kHost, true), "Any admits a guest on the host");
    check(rule::admits(SenderPolicy::Any, kGuest, kHost, false), "Any admits a guest on a guest");
}

void testHostOnly() {
    check(rule::admits(SenderPolicy::HostOnly, kHost, kHost, false), "HostOnly admits the host");
    check(!rule::admits(SenderPolicy::HostOnly, kGuest, kHost, false), "HostOnly refuses a guest");
    check(!rule::admits(SenderPolicy::HostOnly, kGuest, kHost, true), "HostOnly refuses a guest on the host");
    check(rule::admits(SenderPolicy::HostOnly, kGuest, kGuest, false), "the host slot is whatever the session says");
}

void testGuestOnly() {
    check(rule::admits(SenderPolicy::GuestOnly, kGuest, kHost, true), "GuestOnly admits a guest on the host");
    check(rule::admits(SenderPolicy::GuestOnly, kOtherGuest, kHost, true), "GuestOnly admits any guest slot on the host");
    check(!rule::admits(SenderPolicy::GuestOnly, kHost, kHost, true), "GuestOnly refuses the host slot");
    check(!rule::admits(SenderPolicy::GuestOnly, kGuest, kHost, false), "GuestOnly refuses a request that reached a guest");
    check(!rule::admits(SenderPolicy::GuestOnly, kHost, kHost, false), "GuestOnly refuses the host on a guest");
}

void testNames() {
    check(std::strcmp(rule::name(SenderPolicy::HostOnly), "host only") == 0, "HostOnly is named");
    check(std::strcmp(rule::name(SenderPolicy::GuestOnly), "guest only") == 0, "GuestOnly is named");
    check(std::strcmp(rule::name(SenderPolicy::Any), "any sender") == 0, "Any is named");
}

}  // namespace

int main() {
    testAny();
    testHostOnly();
    testGuestOnly();
    testNames();
    if (g_failures == 0) std::printf("frame_router_rule_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
