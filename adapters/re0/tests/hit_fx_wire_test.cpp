// Checks the HIT_FX and PART_BURST wire formats and the recorder (no game): size, round trip, refusals, the recorder's capacity and its
// silence when a hit made no effect and no sound. Exit 0 when every check passes.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "../src/hit_fx_wire.h"

namespace {

namespace wire = hit_fx_wire;

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

std::vector<uint8_t> bytes(const wire::HitFx& fx) {
    const auto* begin = reinterpret_cast<const uint8_t*>(&fx);
    return std::vector<uint8_t>(begin, begin + sizeof(fx));
}

wire::HitFx recorded() {
    wire::Recorder recorder;
    recorder.start(1000, 7, 0x24, 41, 3);
    recorder.addEffect({1.0f, 2.0f, 3.0f}, 4, 2);
    const float at[3] = {5.0f, 6.0f, 7.0f};
    recorder.addSound(at, 0x10, 0x2a, 1);
    return *recorder.finish();
}

template <typename Mutate>
bool refusedAfter(Mutate mutate) {
    wire::HitFx fx = recorded();
    mutate(fx);
    return !wire::decode(bytes(fx)).has_value();
}

void testRoundTrip() {
    check(wire::kMsgHitFx == 0x0162, "HIT_FX is 0x0162");
    const wire::HitFx sent = recorded();
    const auto got = wire::decode(bytes(sent));
    check(got.has_value(), "a recorded hit decodes");
    if (!got) return;
    check(got->senderMs == 1000 && got->seq == 7 && got->scene == 0x24 && got->spawnRecord == 41 && got->roomEpoch == 3, "the header survives");
    check(got->effects == 1 && got->effect[0].zone == 4 && got->effect[0].attackType == 2 && got->effect[0].point.z == 3.0f, "the effect survives");
    check(got->sounds == 1 && got->sound[0].id == 0x2a && got->sound[0].bank == 0x10 && got->sound[0].flag == 1 && got->sound[0].pos[2] == 7.0f, "the sound survives");
}

void testRefusals() {
    std::vector<uint8_t> shortPayload = bytes(recorded());
    shortPayload.pop_back();
    check(!wire::decode(shortPayload).has_value(), "a payload of the wrong size");
    check(refusedAfter([](wire::HitFx& m) { m.spawnRecord = game::kSpawnRecordCount; }), "a record past the spawn table");
    check(refusedAfter([](wire::HitFx& m) { m.effects = wire::kMaxEffects + 1; }), "too many effects");
    check(refusedAfter([](wire::HitFx& m) { m.sounds = wire::kMaxSounds + 1; }), "too many sounds");
    check(refusedAfter([](wire::HitFx& m) { m.effects = 0; m.sounds = 0; }), "a hit with nothing to show");
    check(refusedAfter([](wire::HitFx& m) { m.pad = 1; }), "a non-zero pad");
    check(refusedAfter([](wire::HitFx& m) { m.sound[0].pad[1] = 1; }), "a sound with a pad");
    check(refusedAfter([](wire::HitFx& m) { m.effect[0].point.x = std::nanf(""); }), "a non-finite hit point");
    check(refusedAfter([](wire::HitFx& m) { m.scene = room_authority_wire::kSceneIdLimit; }), "a scene out of range");
}

void testRecorder() {
    wire::Recorder idle;
    idle.addEffect({}, 0, 0);
    check(!idle.recording(), "calls outside a hit are not recorded");

    wire::Recorder quiet;
    quiet.start(1, 1, 0x24, 1, 1);
    check(!quiet.finish().has_value(), "a hit that made no effect and no sound sends nothing");
    check(!quiet.recording(), "finishing stops the recording");

    wire::Recorder busy;
    busy.start(1, 1, 0x24, 1, 1);
    const float at[3] = {};
    for (int i = 0; i < 10; ++i) {
        busy.addEffect({}, i, 0);
        busy.addSound(at, 0, static_cast<uint32_t>(i), 0);
    }
    const auto fx = busy.finish();
    check(fx && fx->effects == wire::kMaxEffects && fx->sounds == wire::kMaxSounds, "calls past the capacity are dropped");
    check(fx && fx->effect[wire::kMaxEffects - 1].zone == wire::kMaxEffects - 1, "the first calls are kept");
}

void testPartBurst() {
    check(wire::kMsgPartBurst == 0x016a, "PART_BURST is 0x016a");
    const wire::PartBurst sent{1000, 3, 2, 0x24, 41, 2, 0};
    const auto* begin = reinterpret_cast<const uint8_t*>(&sent);
    const std::vector<uint8_t> payload(begin, begin + sizeof(sent));
    const auto got = wire::decodeBurst(payload);
    check(got && got->part == 2 && got->spawnRecord == 41 && got->seq == 3, "a head burst round-trips");
    wire::PartBurst bad = sent;
    bad.part = wire::kMaxPart;
    check(!wire::valid(bad), "a part past the known ones is refused");
    bad = sent;
    bad.spawnRecord = game::kSpawnRecordCount;
    check(!wire::valid(bad), "a record past the spawn table is refused");
    check(!wire::decodeBurst(std::vector<uint8_t>(payload.begin(), payload.end() - 1)).has_value(), "a short burst is refused");
}

void testEnemySounds() {
    check(wire::kMsgEnemySound == 0x016b, "ENEMY_SOUND is 0x016b");
    wire::EnemySoundBatch batch{};
    batch.senderMs = 10;
    batch.seq = 1;
    batch.scene = 0x24;
    batch.count = 1;
    batch.entry[0] = {41, 0, 0, {1.0f, 2.0f, 3.0f}, 0x10, 0x55};
    const auto* begin = reinterpret_cast<const uint8_t*>(&batch);
    const std::vector<uint8_t> payload(begin, begin + sizeof(batch));
    const auto got = wire::decodeSounds(payload);
    check(got && got->count == 1 && got->entry[0].id == 0x55 && got->entry[0].spawnRecord == 41, "a sound batch round-trips");
    wire::EnemySoundBatch bad = batch;
    bad.count = 0;
    check(!wire::valid(bad), "an empty batch is refused");
    bad = batch;
    bad.count = wire::kMaxBatchSounds + 1;
    check(!wire::valid(bad), "an overfull batch is refused");
    bad = batch;
    bad.entry[0].spawnRecord = game::kSpawnRecordCount;
    check(!wire::valid(bad), "a sound for a record past the table is refused");
    check(wire::fromEnemyCode(0x4213f0) && wire::fromEnemyCode(0x4d7d94), "the enemy classes' own calls count");
    check(!wire::fromEnemyCode(0x4f0c4b) && !wire::fromEnemyCode(0x6447e5), "the player's and the sound system's do not");
}

void testPartMask() {
    check(wire::kMsgPartMask == 0x016c, "PART_MASK is 0x016c");
    const wire::PartMask sent{1, 1, 1, 0x24, 41, {0xffff0000u, 3}};
    const auto* begin = reinterpret_cast<const uint8_t*>(&sent);
    const auto got = wire::decodeMask(std::vector<uint8_t>(begin, begin + sizeof(sent)));
    check(got && got->words[0] == 0xffff0000u && got->words[1] == 3, "a part mask round-trips");
    wire::PartMask bad = sent;
    bad.spawnRecord = game::kSpawnRecordCount;
    check(!wire::valid(bad), "a mask for a record past the table is refused");

    wire::MaskTracker<4> tracker;
    const uint32_t intact[wire::kPartMaskWords] = {0xff, 0};
    const uint32_t headless[wire::kPartMaskWords] = {0xfb, 0};
    check(tracker.changed(1, 41, intact), "a new enemy's first mask is sent (a part may be gone already)");
    check(!tracker.changed(1, 41, intact), "an unchanged mask is not sent");
    check(tracker.changed(1, 41, headless), "a changed mask is sent");
    check(!tracker.changed(1, 41, headless), "and only once");
    check(tracker.changed(1, 42, intact), "another enemy in the slot is sent anew");
    tracker.forget(1);
    check(tracker.changed(1, 42, headless), "a forgotten slot is sent anew");
    check(!tracker.changed(1, 42, headless), "then only on a change");
    check(!tracker.changed(9, 1, intact), "a slot past the pool is ignored");
}

}  // namespace

int main() {
    testPartMask();
    testEnemySounds();
    testPartBurst();
    testRoundTrip();
    testRefusals();
    testRecorder();
    if (g_failures == 0) std::printf("hit_fx_wire_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
