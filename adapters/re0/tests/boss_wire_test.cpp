// Checks the boss hand-over wire format (no game): sizes, round trips and every refusal of the decoder. Exit 0 when every
// check passes.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "../src/boss_wire.h"

namespace {

namespace wire = boss_wire;

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

uint8_t bossClassIndex() { return static_cast<uint8_t>(enemy_policy::classIndexOf(game::kBossVtables[0])); }

wire::Member member(uint8_t slot) {
    wire::Member m{};
    m.slot = slot;
    m.classIndex = bossClassIndex();
    m.spawnId = 0x31;
    m.hp = 600;
    m.pos[0] = 10.0f;
    m.pos[2] = -4.0f;
    m.quat = 0x1234;
    m.record[0] = 1;
    m.record[1] = 0x12;
    return m;
}

wire::Message prepare() {
    wire::Message message{};
    message.header.token = (1u << 24) | 1;
    message.header.roomEpoch = 3;
    message.header.scene = 0x24;
    message.header.phase = static_cast<uint8_t>(wire::Phase::Prepare);
    message.header.count = 2;
    message.header.alive = 0x3;
    message.members = {member(4), member(9)};
    return message;
}

wire::Message plain(wire::Phase phase) {
    wire::Message message{};
    message.header.token = (1u << 24) | 1;
    message.header.roomEpoch = 3;
    message.header.scene = 0x24;
    message.header.phase = static_cast<uint8_t>(phase);
    return message;
}

bool decodes(const wire::Message& message) { return wire::decode(wire::encode(message)).has_value(); }

template <typename Change>
bool refusedAfter(wire::Message message, Change change) {
    change(message);
    return !decodes(message);
}

void testLayouts() {
    check(sizeof(wire::Header) == 16, "header is 16 bytes");
    check(sizeof(wire::Member) == 40, "member is 40 bytes");
    check(wire::kMsgBossHandover == 0x0169, "message id 0x0169");
    check(wire::encode(prepare()).size() == 16 + 2 * 40, "a prepare of two members is 96 bytes");
    check(wire::encode(plain(wire::Phase::Commit)).size() == 16, "a commit is the header alone");
}

void testBossClasses() {
    check(wire::bossClass(bossClassIndex()), "uEnemy31 is a boss class");
    int bosses = 0;
    for (size_t i = 0; i < enemy_policy::kClassCount; ++i) bosses += wire::bossClass(static_cast<uint8_t>(i)) ? 1 : 0;
    check(bosses == 4, "four boss classes");
    check(!wire::bossClass(0), "uEnemy10 is not a boss class");
    check(!wire::bossClass(static_cast<uint8_t>(enemy_policy::classIndexOf(0xcc25d8))), "Marcus (Legacy) is not a boss class");
    check(!wire::bossClass(255), "a class past the table is not a boss class");
}

void testRoundTrips() {
    const auto back = wire::decode(wire::encode(prepare()));
    check(back && back->members.size() == 2 && back->members[1].slot == 9 && back->members[0].hp == 600 &&
              back->header.alive == 3 && back->members[0].record[1] == 0x12,
          "a prepare round-trips");
    for (const wire::Phase phase : {wire::Phase::Ready, wire::Phase::Commit}) check(decodes(plain(phase)), "ready and commit decode");
    wire::Message outcome = plain(wire::Phase::Outcome);
    outcome.header.alive = 0x5;
    check(decodes(outcome), "an outcome decodes with any alive mask");
    wire::Message cancel = plain(wire::Phase::Cancel);
    cancel.header.reason = static_cast<uint8_t>(wire::Reason::Member);
    check(decodes(cancel), "a cancel with a reason decodes");
    check(wire::maskOf(8) == 0xff && wire::maskOf(1) == 1 && wire::maskOf(3) == 7, "mask of the first members");
}

void testSizeRefusals() {
    const std::vector<uint8_t> bytes = wire::encode(prepare());
    check(!wire::decode({}).has_value(), "an empty payload");
    check(!wire::decode(std::span<const uint8_t>(bytes.data(), 15)).has_value(), "a payload shorter than the header");
    check(!wire::decode(std::span<const uint8_t>(bytes.data(), bytes.size() - 1)).has_value(), "a partial member");
    check(!wire::decode(std::span<const uint8_t>(bytes.data(), 16 + 40)).has_value(), "fewer members than the header counts");
    wire::Message many = prepare();
    many.members.assign(wire::kMaxMembers + 1, member(0));
    many.header.count = static_cast<uint8_t>(many.members.size());
    check(!decodes(many), "more members than the limit");
}

void testHeaderRefusals() {
    using M = wire::Message;
    check(refusedAfter(prepare(), [](M& m) { m.header.token = 0; }), "token 0");
    check(refusedAfter(prepare(), [](M& m) { m.header.roomEpoch = 0; }), "epoch 0");
    check(refusedAfter(prepare(), [](M& m) { m.header.scene = 0xaf; }), "a scene past the limit");
    check(refusedAfter(prepare(), [](M& m) { m.header.phase = wire::kPhaseCount; }), "a phase past the table");
    check(refusedAfter(prepare(), [](M& m) { m.header.pad = 1; }), "a header pad");
    check(refusedAfter(prepare(), [](M& m) { m.header.count = 1; }), "a count that differs from the members");
    check(refusedAfter(prepare(), [](M& m) { m.header.alive = 0; }), "a prepare with no one alive");
    check(refusedAfter(prepare(), [](M& m) { m.header.alive = 0x7; }), "an alive bit past the members");
    check(refusedAfter(prepare(), [](M& m) { m.header.reason = 1; }), "a prepare with a reason");
    check(refusedAfter(prepare(), [](M& m) { m.members.clear(); m.header.count = 0; }), "a prepare with no members");
}

void testMemberRefusals() {
    using M = wire::Message;
    check(refusedAfter(prepare(), [](M& m) { m.members[0].slot = game::kEnemyPoolSlots; }), "a slot past the pool");
    check(refusedAfter(prepare(), [](M& m) { m.members[0].classIndex = 0; }), "a class that is not a boss");
    check(refusedAfter(prepare(), [](M& m) { m.members[0].spawnRecord = game::kSpawnRecordCount; }), "a spawn record past the table");
    check(refusedAfter(prepare(), [](M& m) { m.members[0].pos[1] = NAN; }), "a non-finite position");
    check(refusedAfter(prepare(), [](M& m) { m.members[0].record[0] = -1; }), "a record state below 0");
    check(refusedAfter(prepare(), [](M& m) { m.members[0].record[1] = 0x1000; }), "an action id past the bound");
    check(refusedAfter(prepare(), [](M& m) { m.members[1].slot = 4; }), "two members in one slot");
}

void testPhaseRules() {
    using M = wire::Message;
    for (const wire::Phase phase : {wire::Phase::Ready, wire::Phase::Commit}) {
        check(refusedAfter(plain(phase), [](M& m) { m.header.alive = 1; }), "ready and commit carry no alive mask");
        check(refusedAfter(plain(phase), [](M& m) { m.header.reason = 1; }), "ready and commit carry no reason");
        check(refusedAfter(plain(phase), [](M& m) { m.members.push_back(member(1)); m.header.count = 1; }), "ready and commit carry no members");
    }
    check(refusedAfter(plain(wire::Phase::Outcome), [](M& m) { m.header.reason = 1; }), "an outcome with a reason");
    check(refusedAfter(plain(wire::Phase::Cancel), [](M& m) { m.header.reason = 0; }), "a cancel without a reason");
    check(refusedAfter(plain(wire::Phase::Cancel), [](M& m) { m.header.reason = wire::kReasonCount; }), "a cancel with a reason past the table");
    check(refusedAfter(plain(wire::Phase::Cancel), [](M& m) { m.header.reason = 1; m.header.alive = 1; }), "a cancel with an alive mask");
}

void testNames() {
    check(std::strcmp(wire::name(wire::Phase::Prepare), "prepare") == 0 && std::strcmp(wire::name(wire::Phase::Cancel), "cancel") == 0,
          "phase names");
    check(std::strcmp(wire::name(wire::Reason::Member), "member") == 0 && std::strcmp(wire::name(wire::Reason::Left), "left") == 0,
          "reason names");
}

}  // namespace

int main() {
    testLayouts();
    testBossClasses();
    testRoundTrips();
    testSizeRefusals();
    testHeaderRefusals();
    testMemberRefusals();
    testPhaseRules();
    testNames();
    if (g_failures == 0) std::printf("boss_wire_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
