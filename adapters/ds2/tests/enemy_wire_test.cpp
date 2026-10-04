// enemy_wire: the ENEMY_* payload checks (no game).
#include <cstdio>
#include <cstring>
#include <vector>

#include "../src/enemy_wire.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

std::vector<uint8_t> bytes(const void* data, size_t size) {
    const auto* begin = static_cast<const uint8_t*>(data);
    return {begin, begin + size};
}

}  // namespace

int main() {
    using namespace enemy_wire;
    EnemySpawn spawn{};
    spawn.netId = 7;
    spawn.entityUuid[15] = 9;
    spawn.pose.position[2] = 182.5;
    EnemySpawn gotSpawn{};
    check(decodeOne(bytes(&spawn, sizeof(spawn)), gotSpawn) && std::memcmp(&gotSpawn, &spawn, sizeof(spawn)) == 0,
          "a spawn round trips");
    check(!decodeOne(std::vector<uint8_t>(sizeof(spawn) - 1), gotSpawn), "a short spawn is rejected");

    EnemyGone gone{3, static_cast<uint8_t>(GoneReason::Died), 0};
    EnemyGone gotGone{};
    check(decodeOne(bytes(&gone, sizeof(gone)), gotGone) && gotGone.netId == 3 && gotGone.reason == 1,
          "a gone event round trips");

    std::vector<EnemyState> states(3);
    for (size_t i = 0; i < states.size(); ++i) {
        states[i].netId = static_cast<uint16_t>(i + 1);
        states[i].flags = i == 2 ? kStateDead : 0;
        states[i].velocity[1] = 1.5f * static_cast<float>(i);
    }
    std::vector<EnemyState> got;
    check(decodeStates(encodeStates(states), got) && got.size() == 3 &&
              std::memcmp(got.data(), states.data(), 3 * sizeof(EnemyState)) == 0,
          "a state batch round trips");
    check(decodeStates(encodeStates({}), got) && got.empty(), "an empty batch round trips");

    std::vector<uint8_t> shortBatch = encodeStates(states);
    shortBatch.pop_back();
    check(!decodeStates(shortBatch, got), "a truncated batch is rejected");
    std::vector<EnemyState> tooMany(kMaxStatesPerMessage + 1);
    check(!decodeStates(encodeStates(tooMany), got), "an oversized batch is rejected");
    check(!decodeStates(std::vector<uint8_t>{1}, got), "a payload without a count is rejected");

    EnemyAnim anim;
    anim.netId = 12;
    anim.snapshot = true;
    remote_animation::Change change{};
    change.index = 7;
    change.type = remote_animation::kTypeFloat;
    const float speed = 2.5f;
    std::memcpy(change.value, &speed, sizeof(speed));
    anim.changes.push_back(change);
    EnemyAnim gotAnim;
    check(decodeAnim(encodeAnim(anim), gotAnim) && gotAnim.netId == 12 && gotAnim.snapshot && gotAnim.changes.size() == 1 &&
              gotAnim.changes[0].index == 7 && std::memcmp(gotAnim.changes[0].value, &speed, sizeof(speed)) == 0,
          "an animation report round trips");
    check(!decodeAnim(std::vector<uint8_t>{1, 0}, gotAnim), "an animation report without a body is rejected");

    std::printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
    return g_failures ? 1 : 0;
}
