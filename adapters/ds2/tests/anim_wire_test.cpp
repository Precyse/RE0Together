// anim_wire: the ANIM_STATE / ANIM_EVENT payload with and without a timestamp, pulse detection, and how long a received
// pulse is held (no game).
#include <cstdio>
#include <cstring>
#include <vector>

#include "../src/anim_event.h"
#include "../src/anim_wire.h"
#include "../src/pulse_detector.h"
#include "../src/reject_counters.h"

namespace {

using remote_animation::Change;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

Change boolChange(uint16_t index, bool value) {
    Change change{};
    change.index = index;
    change.type = remote_animation::kTypeBool;
    change.value[0] = value;
    return change;
}

Change floatChange(uint16_t index, float value) {
    Change change{};
    change.index = index;
    change.type = remote_animation::kTypeFloat;
    std::memcpy(change.value, &value, sizeof(value));
    return change;
}

Change quatChange(uint16_t index) {
    Change change{};
    change.index = index;
    change.type = remote_animation::kTypeQuat;
    for (size_t i = 0; i < remote_animation::kMaxValueBytes; ++i) change.value[i] = static_cast<uint8_t>(i + 1);
    return change;
}

void codec() {
    const std::vector<Change> sent{boolChange(7, true), floatChange(300, 1.5f), quatChange(9)};
    const std::vector<uint8_t> payload = anim_wire::encode(42, true, 123'456'789, sent);
    check(payload.size() == sizeof(anim_wire::AnimHeader) + 8 + (3 + 1) + (3 + 4) + (3 + 16), "the payload is header, stamp and sized entries");

    anim_wire::Report report;
    check(anim_wire::decode(payload, report), "a payload decodes");
    check(report.header.seq == 42 && report.header.count == 3, "the header survives");
    check((report.header.flags & anim_wire::kFlagSnapshot) && (report.header.flags & anim_wire::kFlagTimestamped), "snapshot and timestamp flags are set");
    check(report.sentUs && *report.sentUs == 123'456'789, "the timestamp survives");
    check(report.changes.size() == 3 && report.changes[0].index == 7 && report.changes[0].value[0] == 1 &&
              report.changes[1].index == 300 && std::memcmp(report.changes[1].value, sent[1].value, 4) == 0 &&
              report.changes[2].type == remote_animation::kTypeQuat &&
              std::memcmp(report.changes[2].value, sent[2].value, remote_animation::kMaxValueBytes) == 0,
          "entries of every type survive");
    anim_wire::Report delta;
    anim_wire::decode(anim_wire::encode(1, false, 0, sent), delta);
    check((delta.header.flags & anim_wire::kFlagSnapshot) == 0, "a delta report has no snapshot flag");
}

void oldLayout() {
    // The layout before timestamps: header, then entries, no kFlagTimestamped.
    std::vector<uint8_t> old(sizeof(anim_wire::AnimHeader));
    const anim_wire::AnimHeader header{5, 1, 0};
    std::memcpy(old.data(), &header, sizeof(header));
    const uint16_t index = 11;
    old.insert(old.end(), reinterpret_cast<const uint8_t*>(&index), reinterpret_cast<const uint8_t*>(&index) + 2);
    old.push_back(remote_animation::kTypeBool);
    old.push_back(1);
    anim_wire::Report report;
    check(anim_wire::decode(old, report) && !report.sentUs && report.changes.size() == 1 && report.changes[0].index == 11,
          "a report without a timestamp still decodes, with none");
    old.push_back(0xEE);
    check(anim_wire::decode(old, report), "bytes after the last entry are ignored");
}

void rejections() {
    const std::vector<uint8_t> payload = anim_wire::encode(1, false, 99, std::vector<Change>{floatChange(1, 2.0f)});
    anim_wire::Report report;
    check(!anim_wire::decode(std::span<const uint8_t>(payload.data(), 7), report), "shorter than the header is rejected");
    check(!anim_wire::decode(std::span<const uint8_t>(payload.data(), 12), report), "a cut-off timestamp is rejected");
    check(!anim_wire::decode(std::span<const uint8_t>(payload.data(), payload.size() - 1), report) && report.changes.empty(),
          "a cut-off entry is rejected whole");
    std::vector<uint8_t> badType = payload;
    badType[sizeof(anim_wire::AnimHeader) + 8 + 2] = 9;
    check(!anim_wire::decode(badType, report), "an entry of an unknown type is rejected");
    check(anim_wire::decode(anim_wire::encode(2, false, 0, {}), report) && report.changes.empty(), "an empty report is valid");
}

void pulses() {
    PulseDetector detector;
    constexpr TimeUs kMs = 1000;
    check(!detector.onBool(1, false, 0), "the first sight of a variable is never a pulse");
    check(!detector.onBool(1, false, 100 * kMs), "an unchanged value is not a pulse");
    check(!detector.onBool(1, true, 200 * kMs), "leaving the resting value is not yet known to be a pulse");
    const auto pulse = detector.onBool(1, false, 300 * kMs);
    check(pulse && *pulse == true, "coming back within the window ends a pulse of the value it held");

    check(!detector.onBool(2, false, 0) && !detector.onBool(2, true, 1000 * kMs), "a variable that goes up...");
    check(!detector.onBool(2, false, 2000 * kMs), "...and stays up for longer than the window is a state, not a pulse");

    check(!detector.onBool(3, true, 0) && !detector.onBool(3, false, 50 * kMs), "a variable that rests true: it fell once");
    const auto dip = detector.onBool(3, true, 150 * kMs);
    check(dip && *dip == false, "a quick dip from true is a pulse of false");
    detector.clear();
    check(!detector.onBool(1, true, 0), "clear forgets every variable");
}

void heldEvents() {
    reject_counters::reset();
    GameFrame frame;
    frame.type = anim_wire::kMsgAnimEvent;
    frame.slot = 1;
    frame.payload = anim_wire::encode(1, false, 5, std::vector<Change>{boolChange(4, true)});
    anim_event::onFrame(frame, 1'000'000);
    check(anim_event::active(1, 1'000'000 + anim_event::kHoldUs - 1).size() == 1, "an event is held for the hold time");
    check(anim_event::active(2, 1'000'000).empty(), "another slot does not see it");
    check(anim_event::active(1, 1'000'000 + anim_event::kHoldUs).empty(), "then it is gone");

    frame.payload.pop_back();
    anim_event::onFrame(frame, 5'000'000);
    check(anim_event::active(1, 5'000'000).empty() && reject_counters::total(anim_wire::kMsgAnimEvent, reject_counters::Reason::Malformed) == 1,
          "a malformed event is counted and held for nothing");
}

}  // namespace

int main() {
    codec();
    oldLayout();
    rejections();
    pulses();
    heldEvents();
    std::printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
    return g_failures ? 1 : 0;
}
