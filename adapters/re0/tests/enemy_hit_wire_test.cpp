#include <cstddef>
#include <cstdio>
#include <cstring>

#include "../src/enemy_hit_wire.h"

namespace {

using enemy_protocol::HitPayload;

constexpr size_t kPointOffset = 4;  // after slot, attacker, flag, room
static_assert(offsetof(HitPayload, point) == kPointOffset);
static_assert(sizeof(HitPayload) == kPointOffset + sizeof(game::HitPoint) + 4 * sizeof(int32_t));

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    ++g_failures;
    std::printf("FAIL: %s\n", what);
}

}  // namespace

int main() {
    const game::HitPoint point{3551.85f, 412.5f, -2845.25f};
    int senderAttacker = 0;
    const game::HitInfo info{2, 0x1b, 7, -3, &senderAttacker, 1, {}};
    const HitPayload sent = enemy_hit_wire::encode(5, 1, 0x25, point, info);

    unsigned char wire[sizeof(HitPayload)];
    std::memcpy(wire, &sent, sizeof(wire));
    float wirePoint[3];
    std::memcpy(wirePoint, wire + kPointOffset, sizeof(wirePoint));
    check(wirePoint[0] == point.x && wirePoint[1] == point.y && wirePoint[2] == point.z,
          "the point's three floats are on the wire");

    HitPayload received;
    std::memcpy(&received, wire, sizeof(received));
    check(received.slot == 5 && received.attackerCharacterId == 1 && received.room == 0x25 && received.flag == 1,
          "header fields");

    const game::HitPoint decoded = enemy_hit_wire::pointOf(received);
    check(decoded.x == point.x && decoded.y == point.y && decoded.z == point.z, "point decodes to the same values");

    int receiverAttacker = 0;
    const game::HitInfo rebuilt = enemy_hit_wire::infoOf(received, &receiverAttacker);
    check(rebuilt.rangeTier == 2 && rebuilt.attackType == 0x1b && rebuilt.a == 7 && rebuilt.b == -3 &&
              rebuilt.flag == 1,
          "HitInfo fields survive");
    check(rebuilt.attacker == &receiverAttacker, "the receiver's own attacker object, never the sender's pointer");

    if (g_failures == 0) std::printf("enemy_hit_wire_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
