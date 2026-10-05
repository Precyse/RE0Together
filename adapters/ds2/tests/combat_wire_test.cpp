// combat_wire, combat_rules and enemy_directory: payload round trips, hit validation, the hit rate limit, idempotent
// deaths and the net id / UUID directory (no game).
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

#include "../src/combat_rules.h"
#include "../src/combat_wire.h"
#include "../src/enemy_directory.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

template <class T>
std::vector<uint8_t> bytes(const T& value) {
    const auto* begin = reinterpret_cast<const uint8_t*>(&value);
    return {begin, begin + sizeof(T)};
}

combat_wire::EnemyHit sampleHit() {
    combat_wire::EnemyHit hit{};
    hit.enemy.netId = 12;
    hit.enemy.uuid[0] = 0xAB;
    hit.enemy.uuid[15] = 0xCD;
    hit.hit.amount = 85.5f;
    hit.hit.flags = 0x42;
    hit.hit.partIndex = 3;
    hit.hit.origin[2] = 1.75f;
    hit.hit.impulse[1] = -4.0f;
    hit.hit.normal[0] = 1.0f;
    return hit;
}

void wireRoundTrips() {
    using namespace combat_wire;
    const EnemyHit hit = sampleHit();
    EnemyHit got{};
    check(decode(bytes(hit), got) && std::memcmp(&got, &hit, sizeof(hit)) == 0, "an enemy hit round trips");
    check(!decode(std::vector<uint8_t>(sizeof(hit) - 1), got), "a short enemy hit is rejected");
    check(!decode(std::vector<uint8_t>(sizeof(hit) + 1), got), "a long enemy hit is rejected");

    PlayerHit playerHit{};
    playerHit.attacker = hit.enemy;
    playerHit.hit = hit.hit;
    PlayerHit gotPlayer{};
    check(decode(bytes(playerHit), gotPlayer) && gotPlayer.attacker.netId == 12, "a player hit round trips");
    playerHit.attacker = {};
    check(decode(bytes(playerHit), gotPlayer) && isNone(gotPlayer.attacker),
          "a player hit with no enemy behind it is accepted");

    check(combat_wire::kMsgEnemyHit == 0x0120 && combat_wire::kMsgPlayerHit == 0x0121, "the message ids are 0x0120 and 0x0121");
}

void hitValidation() {
    using namespace combat_wire;
    EnemyHit hit = sampleHit();
    EnemyHit got{};
    hit.hit.amount = 0.0f;
    check(!decode(bytes(hit), got), "a hit of no damage is rejected");
    hit = sampleHit();
    hit.hit.amount = -5.0f;
    check(!decode(bytes(hit), got), "a negative amount is rejected");
    hit.hit.amount = kMaxHitAmount;
    check(decode(bytes(hit), got), "the largest accepted amount passes");
    hit.hit.amount = kMaxHitAmount * 2;
    check(!decode(bytes(hit), got), "an amount beyond the bound is rejected");
    hit.hit.amount = std::numeric_limits<float>::quiet_NaN();
    check(!decode(bytes(hit), got), "a NaN amount is rejected");
    hit = sampleHit();
    hit.hit.impulse[0] = std::numeric_limits<float>::infinity();
    check(!decode(bytes(hit), got), "an infinite vector is rejected");
    hit = sampleHit();
    hit.hit.normal[2] = kMaxVectorComponent * 2;
    check(!decode(bytes(hit), got), "a vector component beyond the bound is rejected");
    hit = sampleHit();
    hit.hit.partIndex = kNoPart;
    check(decode(bytes(hit), got), "a hit on no part is accepted");
    hit.hit.partIndex = kMaxPartIndex + 1;
    check(!decode(bytes(hit), got), "a part index beyond the bound is rejected");
    hit.hit.partIndex = -2;
    check(!decode(bytes(hit), got), "a part index below none is rejected");
    hit = sampleHit();
    std::memset(hit.enemy.uuid, 0, sizeof(hit.enemy.uuid));
    check(!decode(bytes(hit), got), "a hit with no enemy UUID is rejected");
    hit = sampleHit();
    hit.enemy.netId = kNoEnemy;
    check(!decode(bytes(hit), got), "a hit with no net id is rejected");
}

void hitLimit() {
    using combat_rules::HitLimiter;
    HitLimiter limiter;
    TimeUs now = 5'000'000;
    uint32_t accepted = 0;
    for (uint32_t i = 0; i < HitLimiter::kMaxPerEnemy * 2; ++i) accepted += limiter.allow(7, now) ? 1 : 0;
    check(accepted == HitLimiter::kMaxPerEnemy, "one enemy takes at most its per-second share");
    check(limiter.allow(8, now), "another enemy is not held back by it");
    now += HitLimiter::kWindowUs;
    check(limiter.allow(7, now), "the count starts again in the next second");

    HitLimiter flood;
    accepted = 0;
    for (uint32_t i = 0; i < HitLimiter::kMaxTotal * 2; ++i) accepted += flood.allow(static_cast<uint16_t>(i), now) ? 1 : 0;
    check(accepted == HitLimiter::kMaxTotal, "hits on many enemies stop at the total");
    check(flood.allow(1, now + HitLimiter::kWindowUs), "and resume in the next second");
    check(flood.allow(2, now - 1), "a clock that went back opens a new window");
}

void directory() {
    enemy_directory::Uuid a{}, b{};
    a[0] = 1;
    b[0] = 2;
    enemy_directory::set(3, a);
    enemy_directory::set(4, b);
    check(enemy_directory::netIdOf(a) == 3 && enemy_directory::netIdOf(b) == 4, "a UUID gives its net id");
    enemy_directory::set(3, b);
    check(enemy_directory::netIdOf(b) == 3 && enemy_directory::netIdOf(a) == 0 && enemy_directory::all().size() == 1,
          "a UUID belongs to one net id: the newer announcement wins");
    enemy_directory::forget(3);
    check(enemy_directory::netIdOf(b) == 0 && enemy_directory::all().empty(), "a forgotten enemy is gone");
    enemy_directory::set(9, a);
    enemy_directory::clear();
    check(enemy_directory::netIdOf(a) == 0, "clear empties it");
}

}  // namespace

int main() {
    wireRoundTrips();
    hitValidation();
    hitLimit();
    directory();
    if (g_failures) {
        std::printf("%d FAILED\n", g_failures);
    } else {
        std::printf("all passed\n");
    }
    return g_failures ? 1 : 0;
}
