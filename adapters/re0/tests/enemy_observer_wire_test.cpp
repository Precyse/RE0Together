// Checks the enemy observer wire format (no game): header 16 bytes, pose entry 60, motion entry 20, round trips, the
// split at 18 entries and every refusal of the decoders. Exit 0 when every check passes.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <vector>

#include "../src/enemy_observer_wire.h"

namespace {

namespace wire = enemy_observer_wire;

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

wire::PoseEntry poseEntry(uint8_t slot) {
    wire::PoseEntry entry{};
    entry.slot = slot;
    entry.classIndex = 5;
    entry.flags = wire::kFlagAlive;
    entry.target = 1;
    entry.spawnId = 0x1234;
    entry.spawnRecord = 77;
    entry.hp = 150;
    entry.hitAttr = 0x3ff;
    entry.pos[0] = 1.5f;
    entry.pos[1] = -2.0f;
    entry.pos[2] = 300.25f;
    const float quat[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    entry.quat = pose_wire::packQuat(quat);
    entry.motion0 = 0x0123;
    entry.motion1 = pose_wire::kNoMotion;
    entry.frame0 = 12.5f;
    entry.frame1 = pose_wire::packFrame(3.0f);
    entry.record[0] = 2;
    entry.record[1] = 7;
    entry.record[2] = -1;
    entry.record[3] = 9;
    entry.flags6a0c = 0x12;
    return entry;
}

wire::MotionEntry motionEntry(uint8_t slot) {
    return {slot, 1, 0x0042, 0x10, 0.25f, 4.0f, 33, 0x1234};
}

wire::Header header() { return {1000, 0, 7, 0x12, 0, 0}; }

void testLayouts() {
    check(sizeof(wire::Header) == 16, "header is 16 bytes");
    check(sizeof(wire::PoseEntry) == 60, "pose entry is 60 bytes");
    check(sizeof(wire::MotionEntry) == 20, "motion entry is 20 bytes");
    check(sizeof(wire::Header) + wire::kMaxEntries * sizeof(wire::PoseEntry) == 1096, "18 pose entries make a 1096 byte packet");
    check(wire::kMsgEnemyPose == 0x0160 && wire::kMsgEnemyMotion == 0x0161, "message ids");
}

void testPoseRoundTrip() {
    uint32_t seq = 40;
    const std::vector<wire::PoseEntry> entries = {poseEntry(3), poseEntry(9)};
    const auto payloads = wire::encode<wire::PoseEntry>(header(), entries, seq);
    check(payloads.size() == 1 && seq == 41, "two entries make one batch and take one number");
    check(payloads[0].size() == 16 + 2 * 60, "payload size");
    const auto batch = wire::decode<wire::PoseEntry>(payloads[0]);
    check(batch.has_value(), "a batch decodes");
    if (!batch) return;
    check(batch->header.seq == 41 && batch->header.count == 2 && batch->header.roomEpoch == 7 && batch->header.scene == 0x12 &&
              batch->header.senderMs == 1000,
          "header fields");
    const wire::PoseEntry& second = batch->entries[1];
    check(second.slot == 9 && second.classIndex == 5 && second.flags == wire::kFlagAlive && second.target == 1, "identity fields");
    check(second.spawnId == 0x1234 && second.spawnRecord == 77 && second.hp == 150 && second.hitAttr == 0x3ff, "spawn, hp, attributes");
    check(second.pos[2] == 300.25f && second.frame0 == 12.5f && second.motion0 == 0x0123 && second.motion1 == pose_wire::kNoMotion,
          "position, frame, motions");
    check(second.record[2] == -1 && second.record[3] == 9 && second.flags6a0c == 0x12, "record and flags");
}

void testMotionRoundTrip() {
    uint32_t seq = 0;
    const std::vector<wire::MotionEntry> entries = {motionEntry(4)};
    const auto payloads = wire::encode<wire::MotionEntry>(header(), entries, seq);
    const auto batch = wire::decode<wire::MotionEntry>(payloads.at(0));
    check(batch.has_value() && batch->entries.size() == 1, "a motion batch decodes");
    if (!batch) return;
    const wire::MotionEntry& entry = batch->entries[0];
    check(entry.slot == 4 && entry.layer == 1 && entry.motionNo == 0x42 && entry.flags == 0x10 && entry.interp == 0.25f &&
              entry.start == 4.0f && entry.dtMs == 33 && entry.seqLow == 0x1234,
          "motion fields");
}

void testSplitAtEighteen() {
    std::vector<wire::PoseEntry> entries;
    for (uint8_t slot = 0; slot < 37; ++slot) entries.push_back(poseEntry(slot));
    uint32_t seq = 0;
    const auto payloads = wire::encode<wire::PoseEntry>(header(), entries, seq);
    check(payloads.size() == 3 && seq == 3, "37 entries make three batches with three numbers");
    const auto first = wire::decode<wire::PoseEntry>(payloads[0]);
    const auto last = wire::decode<wire::PoseEntry>(payloads[2]);
    check(first && first->entries.size() == 18 && first->header.seq == 1, "the first batch holds 18");
    check(last && last->entries.size() == 1 && last->header.seq == 3 && last->entries[0].slot == 36, "the last holds the 37th");
    check(wire::encode<wire::PoseEntry>(header(), std::span<const wire::PoseEntry>{}, seq).empty(), "no entries, no batch");
}

void testHeaderRefusals() {
    uint32_t seq = 0;
    const std::vector<wire::PoseEntry> entries = {poseEntry(1)};
    std::vector<uint8_t> payload = wire::encode<wire::PoseEntry>(header(), entries, seq).at(0);
    check(wire::decode<wire::PoseEntry>(payload).has_value(), "the good payload decodes");
    check(!wire::decode<wire::MotionEntry>(payload).has_value(), "a pose payload is no motion batch");

    std::vector<uint8_t> truncated(payload.begin(), payload.end() - 1);
    check(!wire::decode<wire::PoseEntry>(truncated).has_value(), "a truncated batch is refused");
    std::vector<uint8_t> longer = payload;
    longer.push_back(0);
    check(!wire::decode<wire::PoseEntry>(longer).has_value(), "a batch with trailing bytes is refused");
    check(!wire::decode<wire::PoseEntry>(std::span<const uint8_t>(payload.data(), 8)).has_value(), "a short header is refused");

    std::vector<uint8_t> noCount = payload;
    noCount[offsetof(wire::Header, count)] = 0;
    check(!wire::decode<wire::PoseEntry>(noCount).has_value(), "zero entries are refused");
    std::vector<uint8_t> tooMany = payload;
    tooMany[offsetof(wire::Header, count)] = static_cast<uint8_t>(wire::kMaxEntries + 1);
    check(!wire::decode<wire::PoseEntry>(tooMany).has_value(), "more than 18 entries are refused");
    std::vector<uint8_t> padded = payload;
    padded[offsetof(wire::Header, pad)] = 1;
    check(!wire::decode<wire::PoseEntry>(padded).has_value(), "a non-zero pad is refused");
    std::vector<uint8_t> farScene = payload;
    farScene[offsetof(wire::Header, scene)] = 0xaf;
    check(!wire::decode<wire::PoseEntry>(farScene).has_value(), "a scene past the engine's limit is refused");
}

wire::PoseEntry mutated(void (*change)(wire::PoseEntry&)) {
    wire::PoseEntry entry = poseEntry(2);
    change(entry);
    return entry;
}

void testEntryRefusals() {
    check(wire::valid(poseEntry(36)), "slot 36 is the last pool slot");
    check(!wire::valid(mutated([](wire::PoseEntry& e) { e.slot = game::kEnemyPoolSlots; })), "a slot past the pool is refused");
    check(wire::valid(mutated([](wire::PoseEntry& e) { e.slot = 60; })), "an insect's slot (uEnemy2b table, 60) is valid");
    check(!wire::valid(mutated([](wire::PoseEntry& e) { e.classIndex = 38; })), "class index 38 is refused");
    check(wire::valid(mutated([](wire::PoseEntry& e) { e.spawnRecord = 999; })), "spawn record 999 is accepted");
    check(!wire::valid(mutated([](wire::PoseEntry& e) { e.spawnRecord = 1000; })), "spawn record 1000 is refused");
    check(!wire::valid(mutated([](wire::PoseEntry& e) { e.flags = 0x10; })), "an unknown flag is refused");
    check(!wire::valid(mutated([](wire::PoseEntry& e) { e.target = 2; })), "a target that is no character is refused");
    check(wire::valid(mutated([](wire::PoseEntry& e) { e.target = wire::kNoTarget; })), "no target is fine");
    check(!wire::valid(mutated([](wire::PoseEntry& e) { e.pos[1] = std::nanf(""); })), "a non-finite position is refused");
    check(!wire::valid(mutated([](wire::PoseEntry& e) { e.frame0 = INFINITY; })), "a non-finite frame is refused");
    check(!wire::valid(mutated([](wire::PoseEntry& e) { e.leaseToken = 1; })), "a token without the leased flag is refused");
    check(!wire::valid(mutated([](wire::PoseEntry& e) { e.flags |= wire::kFlagLeased; })), "the leased flag without a token is refused");
    check(wire::valid(mutated([](wire::PoseEntry& e) { e.flags |= wire::kFlagLeased; e.leaseToken = 7; })), "a leased entry with its token is valid");

    check(wire::valid(motionEntry(36)), "a motion for slot 36 is fine");
    wire::MotionEntry bad = motionEntry(game::kEnemyPoolSlots);
    check(!wire::valid(bad), "a motion past the pool is refused");
    bad = motionEntry(1);
    bad.layer = 8;
    check(!wire::valid(bad), "layer 8 is refused");
    bad = motionEntry(1);
    bad.start = std::nanf("");
    check(!wire::valid(bad), "a non-finite start is refused");

    uint32_t seq = 0;
    const std::vector<wire::PoseEntry> mixed = {poseEntry(1), mutated([](wire::PoseEntry& e) { e.slot = game::kEnemyPoolSlots; })};
    check(!wire::decode<wire::PoseEntry>(wire::encode<wire::PoseEntry>(header(), mixed, seq).at(0)).has_value(),
          "one bad entry refuses the batch");
}

void testHpClamp() {
    check(wire::clampHp(150) == 150 && wire::clampHp(-1) == -1, "small hp unchanged");
    check(wire::clampHp(100000) == INT16_MAX && wire::clampHp(-100000) == INT16_MIN, "hp clamps to 16 bits");
}

}  // namespace

int main() {
    testLayouts();
    testPoseRoundTrip();
    testMotionRoundTrip();
    testSplitAtEighteen();
    testHeaderRefusals();
    testEntryRefusals();
    testHpClamp();
    if (g_failures == 0) std::printf("enemy_observer_wire_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
