#include <cstddef>
#include <cstdio>
#include <cstring>

#include "../src/enemy_hit_wire.h"

namespace {

using enemy_protocol::HitPayload;

constexpr size_t kOffsetOffset = 4;  // after slot, attacker, flag, room
constexpr size_t kInfoWords = 4;
constexpr size_t kHpWords = 2;  // before and after
constexpr size_t kRandomOffset =
    kOffsetOffset + sizeof(game::HitPoint) + kInfoWords * sizeof(int32_t) + kHpWords * sizeof(int32_t);
static_assert(offsetof(HitPayload, offset) == kOffsetOffset);
static_assert(offsetof(HitPayload, random) == kRandomOffset);
static_assert(sizeof(HitPayload) == kRandomOffset + sizeof(game::RandomState));

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    ++g_failures;
    std::printf("FAIL: %s\n", what);
}

}  // namespace

int main() {
    const game::HitPoint point{3551.5f, 412.5f, -2845.25f};
    const float senderEnemy[3] = {3500.0f, 300.0f, -2800.0f};
    int senderAttacker = 0;
    const game::HitInfo info{2, 0x1b, 7, -3, &senderAttacker, 1, {}};
    HitPayload sent = enemy_hit_wire::encode(5, 1, 0x25, point, senderEnemy, info);
    check(sent.hpBefore == 0 && sent.hpAfter == 0 && sent.random[0] == 0, "a request carries no outcome");
    const game::RandomState random{0x12345678u, 0x9abcdef0u, 0x0badf00du, 0xdeadbeefu};
    enemy_hit_wire::stamp(sent, 94, random);

    unsigned char wire[sizeof(HitPayload)];
    std::memcpy(wire, &sent, sizeof(wire));
    float wireOffset[3];
    std::memcpy(wireOffset, wire + kOffsetOffset, sizeof(wireOffset));
    check(wireOffset[0] == 51.5f && wireOffset[1] == 112.5f && wireOffset[2] == -45.25f,
          "the point travels relative to the sender's enemy");

    HitPayload received;
    std::memcpy(&received, wire, sizeof(received));
    check(received.slot == 5 && received.attackerCharacterId == 1 && received.room == 0x25 && received.flag == 1,
          "header fields");
    check(received.hpBefore == 94 && enemy_hit_wire::randomOf(received) == random, "the owner's HP and random state");

    const float receiverEnemy[3] = {3510.0f, 300.0f, -2790.0f};
    const game::HitPoint rebuilt = enemy_hit_wire::pointOf(received, receiverEnemy);
    check(rebuilt.x == 3561.5f && rebuilt.y == 412.5f && rebuilt.z == -2835.25f,
          "the point is rebuilt around the receiver's enemy, same height above it");

    int receiverAttacker = 0;
    const game::HitInfo rebuiltInfo = enemy_hit_wire::infoOf(received, &receiverAttacker);
    check(rebuiltInfo.rangeTier == 2 && rebuiltInfo.attackType == 0x1b && rebuiltInfo.a == 7 && rebuiltInfo.b == -3 &&
              rebuiltInfo.flag == 1,
          "HitInfo fields survive");
    check(rebuiltInfo.attacker == &receiverAttacker, "the receiver's own attacker object, never the sender's pointer");

    if (g_failures == 0) std::printf("enemy_hit_wire_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
