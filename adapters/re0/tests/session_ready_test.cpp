// Checks the session-ready handshake rules and wire format (no game). Exit 0 when every check passes.
#include <cstdio>
#include <cstring>
#include <vector>

#include "../src/session_ready_rule.h"

namespace {

namespace rule = session_ready_rule;
namespace wire = session_wire;
using peer_sight::Sight;
using wire::ReadyPhase;
using wire::SessionReady;

constexpr uint16_t kScene = 0x2d;
constexpr uint16_t kLoad = 7;
constexpr uint32_t kSerial = 3;
constexpr uint32_t kNow = 100000;
int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

SessionReady readyAt(uint16_t scene, uint16_t load, uint32_t serial) {
    return {static_cast<uint8_t>(ReadyPhase::Ready), 0, scene, load, 0, serial};
}

Sight poseAt(uint16_t room, uint16_t load, uint32_t arrival) { return {room, load, 0, 0, arrival}; }

std::vector<uint8_t> bytesOf(const SessionReady& ready) {
    std::vector<uint8_t> out(sizeof(ready));
    std::memcpy(out.data(), &ready, sizeof(ready));
    return out;
}

void testWire() {
    check(wire::kMsgSessionReady == 0x0155 && wire::kMsgJoinExtras == 0x0156, "the ids are 0x0155 and 0x0156");
    const auto got = wire::decodeReady(bytesOf(readyAt(kScene, kLoad, kSerial)));
    check(got && got->scene == kScene && got->loadSerial == kLoad && got->snapshotSerial == kSerial, "a good ready decodes");
    check(!wire::decodeReady(std::vector<uint8_t>(11)), "a short ready is refused");
    check(!wire::decodeReady(std::vector<uint8_t>(13)), "a long ready is refused");
    SessionReady bad = readyAt(kScene, kLoad, kSerial);
    bad.phase = 2;
    check(!wire::decodeReady(bytesOf(bad)), "an unknown phase is refused");
    check(!wire::decodeReady(bytesOf(readyAt(wire::kSceneLimit, kLoad, kSerial))), "a scene the game does not have is refused");
    check(!wire::decodeReady(bytesOf(readyAt(0xffff, kLoad, kSerial))), "no scene is refused");
    check(!wire::decodeReady(bytesOf(readyAt(kScene, 0, kSerial))), "load serial 0 is refused");
    check(!wire::decodeReady(bytesOf(readyAt(kScene, kLoad, 0))), "snapshot serial 0 is refused");
    bad = readyAt(kScene, kLoad, kSerial);
    bad.reserved2 = 1;
    check(!wire::decodeReady(bytesOf(bad)), "reserved bytes must be zero");
}

void testJudge() {
    const SessionReady ready = readyAt(kScene, kLoad, kSerial);
    check(rule::judge(ready, poseAt(kScene, kLoad, kNow - 100), kSerial, kNow) == rule::Verdict::Accept,
          "a matching fresh pose is accepted");
    check(rule::judge(ready, std::nullopt, kSerial, kNow) == rule::Verdict::NoPose, "no pose is refused");
    check(rule::judge(ready, poseAt(kScene, kLoad, kNow - peer_sight::kFreshMs - 1), kSerial, kNow) == rule::Verdict::StalePose,
          "a stale pose is refused");
    check(rule::judge(ready, poseAt(kScene, kLoad, kNow - peer_sight::kFreshMs), kSerial, kNow) == rule::Verdict::Accept,
          "a pose exactly at the limit is still fresh");
    check(rule::judge(ready, poseAt(kScene + 1, kLoad, kNow), kSerial, kNow) == rule::Verdict::OtherRoom,
          "a pose in another room is refused");
    check(rule::judge(ready, poseAt(kScene, kLoad + 1, kNow), kSerial, kNow) == rule::Verdict::OtherLoad,
          "a pose of another load of the same room is refused");
    check(rule::judge(readyAt(kScene, kLoad, kSerial + 1), poseAt(kScene, kLoad, kNow), kSerial, kNow) ==
              rule::Verdict::UnknownSnapshot,
          "a snapshot serial the host never issued is refused");
    check(rule::judge(readyAt(kScene, kLoad, kSerial - 1), poseAt(kScene, kLoad, kNow), kSerial, kNow) == rule::Verdict::Accept,
          "an older snapshot serial is accepted: the pose decides");
    check(rule::judge(ready, poseAt(kScene, kLoad, kNow + 5), kSerial, kNow) == rule::Verdict::Accept,
          "a pose stamped a moment after the clock read is fresh");
    const uint32_t wrapNow = 50;
    check(rule::judge(ready, poseAt(kScene, kLoad, 0xffffff00u), kSerial, wrapNow) == rule::Verdict::Accept,
          "freshness survives the clock wrapping");
}

void testEcho() {
    const SessionReady echo = rule::echoOf(readyAt(kScene, kLoad, kSerial));
    check(echo.phase == static_cast<uint8_t>(ReadyPhase::Echo) && echo.scene == kScene && echo.loadSerial == kLoad &&
              echo.snapshotSerial == kSerial,
          "the echo repeats the accepted values in the Echo phase");
}

void testClientHandshake() {
    rule::ReadyClient client;
    check(!client.next(kNow, kScene, kLoad), "nothing is sent before a snapshot with extras is applied");
    client.arm(kSerial);
    const auto first = client.next(kNow, kScene, kLoad);
    check(first && first->snapshotSerial == kSerial && first->scene == kScene && first->loadSerial == kLoad &&
              first->phase == static_cast<uint8_t>(ReadyPhase::Ready),
          "the ready goes out at once");
    check(!client.next(kNow + rule::kResendMs - 1, kScene, kLoad), "it is not repeated before the interval");
    check(client.next(kNow + rule::kResendMs, kScene, kLoad).has_value(), "it is repeated every interval until the echo");
    check(client.next(kNow + rule::kResendMs + 5, kScene + 1, kLoad).has_value(), "a room change is sent at once");
    check(client.next(kNow + rule::kResendMs + 10, kScene + 1, kLoad + 1).has_value(), "a new load of the room is sent at once");
    check(!client.next(kNow + rule::kResendMs + 15, kScene + 1, kLoad + 1), "and then waits again");

    check(!client.onEcho(rule::echoOf(readyAt(kScene, kLoad, kSerial))), "an echo of an earlier ready does not count");
    check(!client.onEcho(readyAt(kScene + 1, kLoad + 1, kSerial)), "a ready is not an echo");
    check(!client.echoed(), "nothing is echoed yet");
    check(client.onEcho(rule::echoOf(readyAt(kScene + 1, kLoad + 1, kSerial))), "the echo of the last ready counts");
    check(client.echoed() && !client.armed(), "the guest is caught up and stops asking");
    check(!client.next(kNow + 10 * rule::kResendMs, kScene + 1, kLoad + 1), "nothing is sent after the echo");
}

void testClientRules() {
    rule::ReadyClient client;
    client.arm(kSerial);
    check(!client.next(kNow, 0xffff, kLoad), "no ready while the room is loading");
    check(!client.next(kNow, kScene, 0), "no ready without a load serial");
    check(!client.onEcho(rule::echoOf(readyAt(kScene, kLoad, kSerial))), "an echo before any ready is ignored");
    client.next(kNow, kScene, kLoad);
    check(!client.onEcho(rule::echoOf(readyAt(kScene, kLoad, kSerial + 1))), "an echo of another snapshot is ignored");
}

void testResyncAndReset() {
    rule::ReadyClient client;
    client.arm(kSerial);
    client.next(kNow, kScene, kLoad);
    client.onEcho(rule::echoOf(readyAt(kScene, kLoad, kSerial)));
    client.arm(kSerial + 1);
    check(client.echoed(), "a resync starts a new handshake but the guest stays caught up");
    const auto again = client.next(kNow + 5, kScene, kLoad);
    check(again && again->snapshotSerial == kSerial + 1, "the new handshake names the new snapshot");
    client.reset();
    check(!client.echoed() && !client.armed() && !client.next(kNow + 10, kScene, kLoad), "a reset forgets everything");
}

}  // namespace

int main() {
    testWire();
    testJudge();
    testEcho();
    testClientHandshake();
    testClientRules();
    testResyncAndReset();
    if (g_failures == 0) std::printf("session_ready_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
