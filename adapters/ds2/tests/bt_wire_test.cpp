// bt_wire: the BT_ENV and CATCHER_EVENT payload round trips, rejections and the region set arithmetic (no game).
#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

#include "../src/bt_wire.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

template <class T>
std::vector<uint8_t> bytes(const T& value) {
    std::vector<uint8_t> out(sizeof(value));
    std::memcpy(out.data(), &value, sizeof(value));
    return out;
}

bt_wire::CatcherEvent territory() {
    bt_wire::CatcherEvent event{};
    event.kind = static_cast<uint8_t>(bt_wire::CatcherKind::Territory);
    event.flags = bt_wire::kFlagTerritoryBool;
    for (size_t i = 0; i < bt_wire::kUuidSize; ++i) event.locator[i] = static_cast<uint8_t>(0xA0 + i);
    return event;
}

}  // namespace

int main() {
    using namespace bt_wire;

    check(kMsgBtEnv == 0x0128 && kMsgCatcherEvent == 0x0129, "the message ids are 0x0128 and 0x0129");

    BtEnv env{0x8000000000000005ull};
    BtEnv gotEnv{};
    check(decode(bytes(env), gotEnv) && gotEnv.activeRegions == env.activeRegions, "BT_ENV round trip");
    std::vector<uint8_t> shortEnv = bytes(env);
    shortEnv.pop_back();
    check(!decode(shortEnv, gotEnv), "a short BT_ENV is rejected");
    std::vector<uint8_t> longEnv = bytes(env);
    longEnv.push_back(0);
    check(!decode(longEnv, gotEnv), "a long BT_ENV is rejected");

    const CatcherEvent sent = territory();
    CatcherEvent got{};
    check(decode(bytes(sent), got) && std::memcmp(&got, &sent, sizeof(sent)) == 0, "CATCHER_EVENT round trip");
    CatcherEvent tar = sent;
    tar.kind = static_cast<uint8_t>(CatcherKind::Tar);
    tar.flags = 0;
    check(decode(bytes(tar), got) && got.kind == tar.kind, "a tar event decodes");
    std::vector<uint8_t> shortEvent = bytes(sent);
    shortEvent.pop_back();
    check(!decode(shortEvent, got), "a short CATCHER_EVENT is rejected");
    CatcherEvent bad = sent;
    bad.kind = 0;
    check(!decode(bytes(bad), got), "kind 0 is rejected");
    bad.kind = 3;
    check(!decode(bytes(bad), got), "an unknown kind is rejected");
    bad = sent;
    bad.flags = 2;
    check(!decode(bytes(bad), got), "unknown flag bits are rejected");

    std::array<uint8_t, kRegionCount> flags{};
    check(maskOf(flags) == 0, "no flag bytes: an empty set");
    flags[0] = 1;
    flags[17] = 0xFF;
    flags[63] = 1;
    const uint64_t mask = maskOf(flags);
    check(mask == ((1ull << 0) | (1ull << 17) | (1ull << 63)), "any non-zero byte is an active region, the top bit included");
    check(isActive(mask, 17) && !isActive(mask, 18) && isActive(mask, 63), "isActive reads single regions");

    check(regionsToChange(mask, mask) == 0, "equal sets change nothing");
    check(regionsToChange(0, mask) == mask, "a guest with none gets every host region");
    check(regionsToChange(mask, 0) == mask, "regions the host cleared are cleared");
    check(regionsToChange(1ull << 5, 1ull << 6) == ((1ull << 5) | (1ull << 6)), "a moved region changes both bits");

    std::printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
    return g_failures ? 1 : 0;
}
