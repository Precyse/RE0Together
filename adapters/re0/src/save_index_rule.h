#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>

// What a save slot's block says about the save, read the way the game's load list reads it (pure, unit tested): the
// same fields and the same label as the launcher's SaveIndex.cs SaveSlot.Label ("Slot 3 / Normal / 2:14:05 / Area 4"),
// so the Co-op page and the hosting screen name a slot alike. Layout: docs/RE0_NOTES.md "Forced save load" and
// coop-scratch/re0/FORCE_LOAD.md 4.1.
namespace save_index_rule {

constexpr size_t kAreaField = 4;
constexpr size_t kUsedFieldsStart = 4;
constexpr size_t kUsedFieldsEnd = 8;
constexpr size_t kWeskerField = 8;
constexpr size_t kDifficultyField = 0x14;
constexpr size_t kPlayFramesField = 0x1c;
constexpr size_t kSummaryBytes = 0x20;  // the fields above all lie in the block's first bytes
constexpr uint8_t kClearedArea = 0x0c;  // the area of a cleared save: Continue starts it in start mode 2
constexpr float kFramesPerSecond = 60.0f;
constexpr double kMaxPlaySeconds = 360000.0;
constexpr int kSecondsPerMinute = 60;
constexpr int kSecondsPerHour = 3600;
constexpr const char* kDifficultyNames[] = {"Easy", "Normal", "Hard"};
constexpr uint32_t kDifficulties = 3;

struct Summary {
    bool used = false;
    uint8_t area = 0;
    bool cleared = false;
    bool wesker = false;
    uint32_t difficulty = 0;
    uint32_t playSeconds = 0;
};

// The summary of a block's first kSummaryBytes bytes (fewer bytes: an unused slot).
inline Summary summarize(std::span<const uint8_t> head) {
    Summary summary;
    if (head.size() < kSummaryBytes) return summary;
    for (size_t i = kUsedFieldsStart; i < kUsedFieldsEnd; ++i) summary.used |= head[i] != 0;
    summary.area = head[kAreaField];
    summary.cleared = summary.area == kClearedArea;
    summary.wesker = head[kWeskerField] != 0;
    std::memcpy(&summary.difficulty, head.data() + kDifficultyField, sizeof(summary.difficulty));
    float frames = 0.0f;
    std::memcpy(&frames, head.data() + kPlayFramesField, sizeof(frames));
    const double seconds = std::isfinite(frames) ? std::round(frames / kFramesPerSecond) : 0.0;
    summary.playSeconds = static_cast<uint32_t>(seconds < 0.0 ? 0.0 : seconds > kMaxPlaySeconds ? kMaxPlaySeconds : seconds);
    return summary;
}

// The launcher's label of slot `index` (numbered from 1).
inline std::string label(int index, const Summary& summary) {
    std::string text = "Slot " + std::to_string(index + 1);
    if (summary.difficulty < kDifficulties) text += std::string(" / ") + kDifficultyNames[summary.difficulty];
    char time[24];
    std::snprintf(time, sizeof(time), " / %u:%02u:%02u", summary.playSeconds / kSecondsPerHour,
                  summary.playSeconds / kSecondsPerMinute % kSecondsPerMinute, summary.playSeconds % kSecondsPerMinute);
    text += time;
    text += summary.cleared ? std::string(" / Cleared") : " / Area " + std::to_string(summary.area);
    if (summary.wesker) text += " / Wesker";
    return text;
}

// Continue's start mode for the slot (sGameInfo +0x74): 1 continue, 2 a cleared save (stateLoadGameWait 0x5eca90).
constexpr int32_t kStartContinue = 1;
constexpr int32_t kStartCleared = 2;
constexpr int32_t startMode(const Summary& summary) { return summary.cleared ? kStartCleared : kStartContinue; }

}  // namespace save_index_rule
