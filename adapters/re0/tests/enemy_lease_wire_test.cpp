// Checks the contact and lease wire format (no game): sizes, round trips and every refusal of the decoders. Exit 0
// when every check passes.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "../src/enemy_lease_wire.h"

namespace {

namespace wire = enemy_lease_wire;

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

wire::EnemyLease lease() {
    wire::EnemyLease message{};
    message.token = 0x10003;
    message.roomEpoch = 4;
    message.senderMs = 1234;
    message.scene = 0x24;
    message.slot = 6;
    message.classIndex = 1;
    message.spawnId = 0x55;
    message.victim = 1;
    message.kind = 0;
    message.level = 0;
    message.hp = 120;
    message.record[0] = 1;
    message.record[1] = 0xe;
    message.record[3] = 1;
    message.pos[0] = 10.0f;
    message.quat = 0x1234;
    message.point[1] = 5.0f;
    return message;
}

wire::EnemyLeaseEnd leaseEnd() {
    wire::EnemyLeaseEnd message{};
    message.token = 0x10003;
    message.roomEpoch = 4;
    message.scene = 0x24;
    message.slot = 6;
    message.reason = static_cast<uint8_t>(contact_rule::End::Released);
    message.hp = 40;
    message.record[0] = 2;
    return message;
}

wire::PlayerHit hit() {
    wire::PlayerHit message{};
    message.roomEpoch = 4;
    message.senderMs = 99;
    message.seq = 7;
    message.scene = 0x24;
    message.slot = 3;
    message.classIndex = 2;
    message.spawnId = 9;
    message.victim = 0;
    message.kind = 0x1c;
    message.level = 3;
    message.point[2] = -4.5f;
    return message;
}

template <typename Message>
std::vector<uint8_t> bytes(const Message& message) {
    const auto* begin = reinterpret_cast<const uint8_t*>(&message);
    return std::vector<uint8_t>(begin, begin + sizeof(Message));
}

template <typename Message, typename Change>
bool refusedAfter(Message message, Change change) {
    change(message);
    return !wire::decode<Message>(bytes(message)).has_value();
}

void testLayouts() {
    check(sizeof(wire::EnemyLease) == 72, "lease is 72 bytes");
    check(sizeof(wire::EnemyLeaseEnd) == 48, "lease end is 48 bytes");
    check(sizeof(wire::PlayerHit) == 36, "player hit is 36 bytes");
    check(wire::kMsgEnemyLease == 0x0163 && wire::kMsgEnemyLeaseEnd == 0x0164 && wire::kMsgPlayerHit == 0x0165, "message ids");
}

void testRoundTrips() {
    const auto l = wire::decode<wire::EnemyLease>(bytes(lease()));
    check(l && l->token == 0x10003 && l->slot == 6 && l->record[1] == 0xe && l->hp == 120 && l->point[1] == 5.0f && l->quat == 0x1234,
          "lease round trip");
    const auto e = wire::decode<wire::EnemyLeaseEnd>(bytes(leaseEnd()));
    check(e && e->hp == 40 && e->reason == static_cast<uint8_t>(contact_rule::End::Released) && e->record[0] == 2, "lease end round trip");
    const auto h = wire::decode<wire::PlayerHit>(bytes(hit()));
    check(h && h->seq == 7 && h->kind == 0x1c && h->level == 3 && h->point[2] == -4.5f, "player hit round trip");
}

void testSizeRefusals() {
    std::vector<uint8_t> payload = bytes(hit());
    payload.push_back(0);
    check(!wire::decode<wire::PlayerHit>(payload).has_value(), "trailing bytes are refused");
    payload.resize(sizeof(wire::PlayerHit) - 1);
    check(!wire::decode<wire::PlayerHit>(payload).has_value(), "a short payload is refused");
    check(!wire::decode<wire::EnemyLease>(bytes(hit())).has_value(), "a hit is no lease");
}

void testLeaseRefusals() {
    using L = wire::EnemyLease;
    check(refusedAfter(lease(), [](L& m) { m.token = 0x10000; }), "a token with zero low bits");
    check(refusedAfter(lease(), [](L& m) { m.scene = 0xaf; }), "a scene past the limit");
    check(refusedAfter(lease(), [](L& m) { m.slot = game::kEnemyPoolSlots; }), "slot past the pool");
    check(refusedAfter(lease(), [](L& m) { m.classIndex = 38; }), "class 38");
    check(refusedAfter(lease(), [](L& m) { m.victim = 2; }), "a victim that is no character");
    check(refusedAfter(lease(), [](L& m) { m.kind = 0x1d; }), "a kind past the table");
    check(refusedAfter(lease(), [](L& m) { m.pad = 1; }), "a non-zero pad");
    check(refusedAfter(lease(), [](L& m) { m.reserved = 1; }), "a non-zero reserved word");
    check(refusedAfter(lease(), [](L& m) { m.spawnRecord = game::kSpawnRecordCount; }), "a lease past the spawn records");
    check(refusedAfter(lease(), [](L& m) { m.hp = 0; }), "a lease on a dead enemy");
    check(refusedAfter(lease(), [](L& m) { m.record[0] = -1; }), "a negative record state");
    check(refusedAfter(lease(), [](L& m) { m.record[1] = 0x1000; }), "an action id past the limit");
    check(refusedAfter(lease(), [](L& m) { m.pos[2] = NAN; }), "a non-finite position");
    check(refusedAfter(lease(), [](L& m) { m.point[0] = INFINITY; }), "a non-finite hit point");
}

void testEndAndHitRefusals() {
    using E = wire::EnemyLeaseEnd;
    check(refusedAfter(leaseEnd(), [](E& m) { m.reason = 0; }), "reason None");
    check(refusedAfter(leaseEnd(), [](E& m) { m.reason = contact_rule::kEndCount; }), "a reason past the list");
    check(refusedAfter(leaseEnd(), [](E& m) { m.slot = game::kEnemyPoolSlots; }), "an end past the pool");
    check(refusedAfter(leaseEnd(), [](E& m) { m.spawnRecord = game::kSpawnRecordCount; }), "an end past the spawn records");
    check(refusedAfter(leaseEnd(), [](E& m) { m.pos[0] = NAN; }), "an end with a non-finite position");
    wire::EnemyLeaseEnd dead = leaseEnd();
    dead.hp = -1;
    check(wire::decode<E>(bytes(dead)).has_value(), "a dead enemy's end is valid");
    using H = wire::PlayerHit;
    check(refusedAfter(hit(), [](H& m) { m.kind = 0x1d; }), "a hit kind past the table");
    check(refusedAfter(hit(), [](H& m) { m.victim = 5; }), "a hit on no character");
    check(refusedAfter(hit(), [](H& m) { m.pad = 1; }), "a hit with a pad");
    check(refusedAfter(hit(), [](H& m) { m.spawnRecord = game::kSpawnRecordCount; }), "a hit past the spawn records");
    check(refusedAfter(hit(), [](H& m) { m.point[1] = NAN; }), "a hit with a non-finite point");
    check(refusedAfter(hit(), [](H& m) { m.scene = 0xff; }), "a hit in a scene past the limit");
}

}  // namespace

int main() {
    testLayouts();
    testRoundTrips();
    testSizeRefusals();
    testLeaseRefusals();
    testEndAndHitRefusals();
    if (g_failures == 0) std::printf("enemy_lease_wire_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
